using Minx.ZMesh.Models;
using NetMQ;
using NetMQ.Sockets;
using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Primitives;

namespace Minx.ZMesh
{
    public class AbstractMessageBox : IAbstractMessageBox, IDisposable
    {
        private readonly string _name;

        private DealerSocket _dealerSocket;
        private NetMQPoller _poller;
        private NetMQQueue<Message> _messageQueue = new NetMQQueue<Message>();

        private MemoryCache _responseCache = new MemoryCache(new MemoryCacheOptions());

        // <ContentType, Message>
        private readonly ConcurrentDictionary<string, ConcurrentQueue<string>> messages = new ConcurrentDictionary<string, ConcurrentQueue<string>>();

        // <ContentType, PendingQuestion>
        private readonly ConcurrentDictionary<string, ConcurrentQueue<PendingQuestion>> _pendingQuestions = new ConcurrentDictionary<string, ConcurrentQueue<PendingQuestion>>();

        // <CorrelationId, PendingQuestion>
        private readonly ConcurrentDictionary<string, PendingQuestion> _pendingQuestionsById = new ConcurrentDictionary<string, PendingQuestion>();

        // <CorrelationId, PendingAnswer>
        private readonly ConcurrentDictionary<string, TaskCompletionSource<Answer>> _pendingAnswers = new ConcurrentDictionary<string, TaskCompletionSource<Answer>>();

        public event EventHandler<MessageReceivedEventArgs> TellReceived;
        public event EventHandler<MessageReceivedEventArgs> QuestionReceived;

        public AbstractMessageBox(string name, string address)
        {
            _name = name;

            _dealerSocket = new DealerSocket();
            _poller = new NetMQPoller { _messageQueue, _dealerSocket };

            _dealerSocket.Options.Identity = System.Text.Encoding.UTF8.GetBytes(Guid.NewGuid().ToString());
            _dealerSocket.Connect("tcp://" + address);

            _messageQueue.ReceiveReady += DequeueAndSendMessage;

            _dealerSocket.ReceiveReady += OnDealerSocketReceiveReady;

            _poller.RunAsync();
        }

        private void DequeueAndSendMessage(object sender, NetMQQueueEventArgs<Message> e)
        {
            if (_messageQueue.TryDequeue(out var message, TimeSpan.Zero))
            {
                var socket = _dealerSocket.SendMoreFrame(message.MessageType.ToString())
                    .SendMoreFrame(message.MessageBoxName ?? string.Empty)
                    .SendMoreFrame(message.ContentType ?? string.Empty);

                if (message is QuestionMessage questionMessage)
                {
                    socket.SendMoreFrame(questionMessage.CorrelationId ?? string.Empty)
                        .SendFrame(questionMessage.Content ?? string.Empty);
                }
                else
                {
                    socket.SendMoreFrame(string.Empty)
                        .SendFrame(message.Content ?? string.Empty);
                }
            }
        }

        internal void WriteTellMessage(Message message)
        {
            var queue = messages.GetOrAdd(message.ContentType, _ => new ConcurrentQueue<string>());

            queue.Enqueue(message.Content);

            TellReceived?.Invoke(this, new MessageReceivedEventArgs(message.ContentType));
        }

        internal void WriteQuestionMessage(PendingQuestion pendingQuestion)
        {
            if (_pendingQuestionsById.TryAdd(pendingQuestion.QuestionMessage.CorrelationId, pendingQuestion))
            {
                var queue = _pendingQuestions.GetOrAdd(pendingQuestion.QuestionMessage.ContentType, _ => new ConcurrentQueue<PendingQuestion>());

                queue.Enqueue(pendingQuestion);

                QuestionReceived?.Invoke(this, new MessageReceivedEventArgs(pendingQuestion.QuestionMessage.ContentType));
            }
            else
            {
                if (_responseCache.TryGetValue(pendingQuestion.QuestionMessage.CorrelationId, out Answer cachedResponse))
                {
                    SendAnswer(pendingQuestion, cachedResponse);
                }
            }
        }

        internal void WriteAnswerMessage(AnswerMessage message)
        {
            if (_pendingAnswers.TryRemove(message.CorrelationId, out var pendingAnswer))
            {
                pendingAnswer.SetResult(new Answer
                {
                    ContentType = message.ContentType,
                    Content = message.Content
                });
            }
        }

        public void Tell(string contentType, string content)
        {
            var tellMessage = new TellMessage
            {
                ContentType = contentType,
                Content = content,
                MessageBoxName = _name
            };

            _messageQueue.Enqueue(tellMessage);
        }

        public bool TryListen(string contentType, Action<string> handler)
        {
            var queue = messages.GetOrAdd(contentType, _ => new ConcurrentQueue<string>());

            if (!queue.TryDequeue(out var message))
            {
                return false;
            }

            handler(message);

            return true;
        }

        public async Task<Answer> Ask(string contentType, string content)
        {
            return await InternalAsk(contentType, content, CancellationToken.None)
                .ConfigureAwait(false);
        }

        public bool TryAnswer(string questionContentType, Func<string, Answer> handler)
        {
            var queue = _pendingQuestions.GetOrAdd(questionContentType, _ => new ConcurrentQueue<PendingQuestion>());

            if (!queue.TryDequeue(out var pendingQuestion))
            {
                return false;
            }

            var correlationId = pendingQuestion.QuestionMessage.CorrelationId;

            var answer = handler(pendingQuestion.QuestionMessage.Content);

            CacheAnswer(correlationId, answer);
            SendAnswer(pendingQuestion, answer);

            return true;
        }

        public async Task<Answer> Ask(string contentType)
        {
            return await InternalAsk(contentType, null, CancellationToken.None)
                .ConfigureAwait(false);
        }

        public async Task<Answer> Ask(string contentType, CancellationToken cancellationToken)
        {
            return await InternalAsk(contentType, null, cancellationToken)
                .ConfigureAwait(false);
        }

        public async Task<Answer> Ask(string contentType, TimeSpan timeout)
        {
            using (var cts = new CancellationTokenSource(timeout))
            {
                return await InternalAsk(contentType, null, cts.Token)
                    .ConfigureAwait(false);
            }
        }

        public async Task<Answer> Ask(string contentType, string content, CancellationToken cancellationToken)
        {
            return await InternalAsk(contentType, content, cancellationToken)
                .ConfigureAwait(false);
        }

        public async Task<Answer> Ask(string contentType, string content, TimeSpan timeout)
        {
            using (var cts = new CancellationTokenSource(timeout))
            {
                return await InternalAsk(contentType, content, cts.Token)
                    .ConfigureAwait(false);
            }
        }

        private async Task<Answer> InternalAsk(string contentType, string content, CancellationToken cancellationToken)
        {
            const int maxRetries = 3;
            const int retryTimeoutMilliseconds = 3000;
            var correlationId = Guid.NewGuid().ToString();

            var questionMessage = new QuestionMessage
            {
                ContentType = contentType,
                Content = content,
                MessageBoxName = _name,
                CorrelationId = correlationId
            };

            var tcs = new TaskCompletionSource<Answer>(TaskCreationOptions.RunContinuationsAsynchronously);

            _pendingAnswers[correlationId] = tcs;

            using (cancellationToken.Register(() =>
            {
                _pendingAnswers.TryRemove(correlationId, out _);
                tcs.TrySetCanceled(cancellationToken);
            }))
            {
                for (int attempt = 0; attempt < maxRetries; attempt++)
                {
                    _messageQueue.Enqueue(questionMessage);

                    try
                    {
                        var completedTask = await Task.WhenAny(tcs.Task, Task.Delay(retryTimeoutMilliseconds, cancellationToken)).ConfigureAwait(false);

                        if (tcs.Task.IsCompleted)
                        {
                            return await tcs.Task.ConfigureAwait(false);
                        }
                    }
                    catch (TaskCanceledException)
                    {
                        if (attempt == maxRetries - 1)
                        {
                            throw new OperationCanceledException("The operation was canceled.", cancellationToken);
                        }
                    }
                }
            }

            throw new TimeoutException($"Failed to get a response after {maxRetries} attempts.");
        }

        public IPendingQuestion GetQuestion(string questionType, out bool available)
        {
            var queue = _pendingQuestions.GetOrAdd(questionType, _ => new ConcurrentQueue<PendingQuestion>());

            if (!queue.TryDequeue(out var pendingQuestion))
            {
                available = false;
                return null;
            }

            available = true;

            return pendingQuestion;
        }

        private void SendAnswer(PendingQuestion pendingQuestion, Answer answer)
        {
            var answerMessage = new AnswerMessage
            {
                ContentType = answer.ContentType,
                Content = answer.Content,
                MessageBoxName = _name,
                CorrelationId = pendingQuestion.QuestionMessage.CorrelationId
            };

            var answerWithIdentity = new IdentityMessage<AnswerMessage>
            {
                Message = answerMessage,
                DealerIdentity = pendingQuestion.DealerIdentity
            };

            pendingQuestion.AnswerQueue.Enqueue(answerWithIdentity);
        }

        private void CacheAnswer(string correlationId, Answer answer)
        {
            _responseCache.Set(correlationId, answer, new MemoryCacheEntryOptions()
            {
                ExpirationTokens =
                {
                    new CancellationChangeToken(new CancellationTokenSource(TimeSpan.FromMinutes(1)).Token)
                },
                PostEvictionCallbacks =
                {
                    new PostEvictionCallbackRegistration
                    {
                        EvictionCallback = OnCacheItemRemoved
                    }
                }
            });
        }

        private void OnCacheItemRemoved(object key, object value, EvictionReason reason, object state)
        {
            _pendingQuestionsById.TryRemove((string)key, out _);
        }

        public void Dispose()
        {
            _poller?.Dispose();
            _dealerSocket?.Dispose();

            foreach (var pendingAnswer in _pendingAnswers.Values)
            {
                pendingAnswer.SetCanceled();
            }

            _pendingAnswers.Clear();
        }
    }
}
        private void OnDealerSocketReceiveReady(object sender, NetMQSocketEventArgs e)
        {
            var socket = e.Socket ?? _dealerSocket;

            var messageTypeString = socket.ReceiveFrameString();
            var messageBoxName = socket.ReceiveFrameString();
            var contentType = socket.ReceiveFrameString();
            var correlationId = socket.ReceiveFrameString();
            var content = socket.ReceiveFrameString();

            var messageType = (MessageType)Enum.Parse(typeof(MessageType), messageTypeString);

            if (messageType != MessageType.Answer)
            {
                return;
            }

            var answerMessage = new AnswerMessage
            {
                MessageBoxName = messageBoxName,
                ContentType = contentType,
                CorrelationId = correlationId,
                Content = content
            };

            WriteAnswerMessage(answerMessage);
        }

