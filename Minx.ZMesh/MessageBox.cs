using Minx.ZMesh.Models;
using Minx.ZMesh.Serialization;
using NetMQ;
using NetMQ.Sockets;
using Newtonsoft.Json;
using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Primitives;


namespace Minx.ZMesh
{
    public class MessageBox : IMessageBox, IDisposable
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
        private readonly ConcurrentDictionary<string, IPendingAnswer> _pendingAnswers = new ConcurrentDictionary<string, IPendingAnswer>();

        public event EventHandler<MessageReceivedEventArgs> TellReceived;
        public event EventHandler<MessageReceivedEventArgs> QuestionReceived;

        public MessageBox(string name, string address)
        {
            _name = name;

            _dealerSocket = new DealerSocket();
            _poller = new NetMQPoller { _messageQueue, _dealerSocket };

            _dealerSocket.Options.Identity = System.Text.Encoding.UTF8.GetBytes(Guid.NewGuid().ToString());
            _dealerSocket.Connect("tcp://" + address);

            _messageQueue.ReceiveReady += DequeueAndSendMessage;

            _dealerSocket.ReceiveReady += (s, e) =>
            {
                var answerMessageJson = _dealerSocket.ReceiveFrameString();

                var answerMessage = JsonConvert.DeserializeObject<AnswerMessage>(answerMessageJson);

                WriteAnswerMessage(answerMessage);
            };

            _poller.RunAsync();
        }

        private void DequeueAndSendMessage(object sender, NetMQQueueEventArgs<Message> e)
        {
            if (_messageQueue.TryDequeue(out var message, TimeSpan.Zero))
            {
                var messageJson = JsonConvert.SerializeObject(message, Newtonsoft.Json.Formatting.Indented, new JsonSerializerSettings()
                {
                    TypeNameHandling = TypeNameHandling.None
                });

                _dealerSocket.SendMoreFrame(message.MessageType.ToString()).SendFrame(messageJson);
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
                if (_responseCache.TryGetValue(pendingQuestion.QuestionMessage.CorrelationId, out string cachedResponse))
                {
                    SendAnswer(pendingQuestion, cachedResponse);
                }
            }
        }

        internal void WriteAnswerMessage(AnswerMessage message)
        {
            if (_pendingAnswers.TryRemove(message.CorrelationId, out var pendingAnswer))
            {
                pendingAnswer.SetAnswer(message.Content);
            }
        }

        public async Task<string> Ask(string contentType)
        {
            return await InternalAsk(contentType, "{}", CancellationToken.None)
                .ConfigureAwait(false);
        }

        public async Task<string> Ask(string contentType, CancellationToken cancellationToken)
        {
            return await InternalAsk(contentType, "{}", cancellationToken)
                .ConfigureAwait(false);
        }

        public async Task<string> Ask(string contentType, TimeSpan timeout)
        {
            using (var cts = new CancellationTokenSource(timeout))
            {
                return await InternalAsk(contentType, "{}", cts.Token)
                    .ConfigureAwait(false);
            }
        }

        public async Task<string> Ask(string contentType, string content)
        {
            return await InternalAsk(contentType, content, CancellationToken.None)
                .ConfigureAwait(false);
        }

        public async Task<string> Ask(string contentType, string content, CancellationToken cancellationToken)
        {
            return await InternalAsk(contentType, content, cancellationToken)
                .ConfigureAwait(false);
        }

        public async Task<string> Ask(string contentType, string content, TimeSpan timeout)
        {
            using (var cts = new CancellationTokenSource(timeout))
            {
                return await InternalAsk(contentType, content, cts.Token)
                    .ConfigureAwait(false);
            }
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question)
        {
            var answerContent = await InternalAsk(typeof(TQuestion).Name, JsonConvert.SerializeObject(question), CancellationToken.None)
                .ConfigureAwait(false);

            return JsonConvert.DeserializeObject<TAnswer>(answerContent);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question, CancellationToken cancellationToken)
        {
            var answerContent = await InternalAsk(typeof(TQuestion).Name, JsonConvert.SerializeObject(question), cancellationToken)
                .ConfigureAwait(false);

            return JsonConvert.DeserializeObject<TAnswer>(answerContent);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question, TimeSpan timeout)
        {
            using (var cts = new CancellationTokenSource(timeout))
            {
                var answerContent = await InternalAsk(typeof(TQuestion).Name, JsonConvert.SerializeObject(question), cts.Token)
                    .ConfigureAwait(false);

                return JsonConvert.DeserializeObject<TAnswer>(answerContent);
            }
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>() where TQuestion : new()
        {
            var answerContent = await InternalAsk(typeof(TQuestion).Name, "{}", CancellationToken.None)
                .ConfigureAwait(false);

            return JsonConvert.DeserializeObject<TAnswer>(answerContent);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(CancellationToken cancellationToken) where TQuestion : new()
        {
            var answerContent = await InternalAsk(typeof(TQuestion).Name, "{}", cancellationToken)
                .ConfigureAwait(false);

            return JsonConvert.DeserializeObject<TAnswer>(answerContent);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TimeSpan timeout) where TQuestion : new()
        {
            using (var cts = new CancellationTokenSource(timeout))
            {
                var answerContent = await InternalAsk(typeof(TQuestion).Name, "{}", cts.Token)
                    .ConfigureAwait(false);

                return JsonConvert.DeserializeObject<TAnswer>(answerContent);
            }
        }

        private async Task<string> InternalAsk(string contentType, string content, CancellationToken cancellationToken)
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

            var tcs = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
            
            var pendingAnswer = new PendingAnswer
            {
                CorrelationId = correlationId,
                TaskCompletionSource = tcs
            };

            _pendingAnswers[correlationId] = pendingAnswer;

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

        public bool TryListen<TMessage>(Action<TMessage> handler)
        {
            var queue = messages.GetOrAdd(typeof(TMessage).Name, _ => new ConcurrentQueue<string>());

            if (!queue.TryDequeue(out var message))
            {
                return false;
            }

            handler(JsonConvert.DeserializeObject<TMessage>(message));

            return true;
        }

        public bool TryListen(string contentType, Action<object> handler)
        {
            var queue = messages.GetOrAdd(contentType, _ => new ConcurrentQueue<string>());

            if (!queue.TryDequeue(out var message))
            {
                return false;
            }

            handler(JsonConvert.DeserializeObject(message, TypeResolver.GetTypeInAllAssemblies(contentType)));

            return true;
        }

        public bool TryAnswer<TQuestion, TAnswer>(Func<TQuestion, TAnswer> handler)
        {
            return TryAnswer(typeof(TQuestion).Name, (q) => handler((TQuestion)q));
        }

        public bool TryAnswer(string questionContentType, Func<object, object> handler)
        {
            var queue = _pendingQuestions.GetOrAdd(questionContentType, _ => new ConcurrentQueue<PendingQuestion>());

            if (!queue.TryDequeue(out var pendingQuestion))
            {
                return false;
            }

            var correlationId = pendingQuestion.QuestionMessage.CorrelationId;

            var questionMessage = JsonConvert.DeserializeObject(pendingQuestion.QuestionMessage.Content, TypeResolver.GetTypeInAllAssemblies(questionContentType));

            var answer = handler(questionMessage);

            var answerContentJson = JsonConvert.SerializeObject(answer, Formatting.Indented, new JsonSerializerSettings()
            {
                TypeNameHandling = TypeNameHandling.None
            });

            CacheAnswer(correlationId, answerContentJson);
            SendAnswer(pendingQuestion, answerContentJson);

            return true;
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

        public void Tell<TMessage>(TMessage message)
        {
            Tell(typeof(TMessage).Name, JsonConvert.SerializeObject(message));
        }

        private void SendAnswer(PendingQuestion pendingQuestion, string answerContentJson)
        {
            var answerMessage = new AnswerMessage
            {
                ContentType = pendingQuestion.QuestionMessage.AnswerContentType,
                Content = answerContentJson,
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

        private void CacheAnswer(string correlationId, string answerContentJson)
        {
            _responseCache.Set(correlationId, answerContentJson, new MemoryCacheEntryOptions()
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
                pendingAnswer.Cancel();
            }

            _pendingAnswers.Clear();
        }
    }
}
