using NetMQ;
using NetMQ.Sockets;
using Newtonsoft.Json;
using System.Collections.Concurrent;
using System.ServiceModel.Channels;
using System.Xml;

namespace DistributedMessanger
{
    public class MessageBox
    {
        private readonly string _name;

        private DealerSocket _dealerSocket;
        private NetMQPoller _poller;
        private NetMQQueue<Message> _messageQueue = new NetMQQueue<Message>();

        // <ContentType, Message>
        private readonly ConcurrentDictionary<string, ConcurrentQueue<string>> _messages = new();

        // <ContentType, PendingQuestion>
        private readonly ConcurrentDictionary<string, ConcurrentQueue<PendingQuestion>> _pendingQuestions = new();

        // <CorrelationId, PendingAnswer>
        private readonly ConcurrentDictionary<string, ConcurrentQueue<IPendingAnswer>> _pendingAnswers = new();

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

        private void DequeueAndSendMessage(object? sender, NetMQQueueEventArgs<Message> e)
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

        internal void WriteMessage(Message message)
        {
            var queue = _messages.GetOrAdd(message.ContentType, _ => new ConcurrentQueue<string>());

            queue.Enqueue(message.Content);
        }

        internal void WriteAnswerMessage(AnswerMessage message)
        {
            if (_pendingAnswers.TryGetValue(message.CorrelationId, out var answerQueue))
            {
                if (answerQueue.TryDequeue(out var pendingAnswer))
                {
                    var type = pendingAnswer.GetAnswerType();
                    var answer = JsonConvert.DeserializeObject(message.Content, type);

                    pendingAnswer.SetAnswer(answer);
                }
            }
        }

        public bool TryListen<TMessage>(Action<TMessage> handler)
        {
            var queue = _messages.GetOrAdd(typeof(TMessage).Name, _ => new ConcurrentQueue<string>());

            if (!queue.TryDequeue(out var message))
            {
                return false;
            }

            handler(JsonConvert.DeserializeObject<TMessage>(message));

            return true;
        }

        public bool TryAnswer<TQuestion, TAnswer>(Func<TQuestion, TAnswer> handler)
        {
            var queue = _pendingQuestions.GetOrAdd(typeof(TQuestion).Name, _ => new ConcurrentQueue<PendingQuestion>());

            if (!queue.TryDequeue(out var pendingQuestion))
            {
                return false;
            }

            var questionMessage = JsonConvert.DeserializeObject<TQuestion>(pendingQuestion.QuestionMessage.Content);

            var answer = handler.DynamicInvoke(new object[] { questionMessage });
            var answerContentJson = JsonConvert.SerializeObject(answer, Newtonsoft.Json.Formatting.Indented, new JsonSerializerSettings()
            {
                TypeNameHandling = TypeNameHandling.None
            });

            var answerMessage = new AnswerMessage
            {
                ContentType = typeof(TAnswer).Name,
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

            return true;
        }

        public void Tell<TMessage>(TMessage message)
        {
            var tellMessage = new TellMessage
            {
                ContentType = typeof(TMessage).Name,
                Content = JsonConvert.SerializeObject(message),
                MessageBoxName = _name
            };

            _messageQueue.Enqueue(tellMessage);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question)
        {
            var correlationId = Guid.NewGuid().ToString();

            var questionMessage = new QuestionMessage
            {
                ContentType = typeof(TQuestion).Name,
                Content = JsonConvert.SerializeObject(question),
                MessageBoxName = _name,
                CorrelationId = correlationId
            };

            var tcs = new TaskCompletionSource<TAnswer>();

            var pendingAnswer = new PendingAnswers<TAnswer>
            {
                CorrelationId = correlationId,
                TaskCompletionSource = tcs
            };

            _pendingAnswers.GetOrAdd(correlationId, _ => new ConcurrentQueue<IPendingAnswer>())
                .Enqueue(pendingAnswer);

            _messageQueue.Enqueue(questionMessage);

            return await tcs.Task;
        }

        internal void WriteQuestion(PendingQuestion pendingQuestion)
        {
            var queue = _pendingQuestions.GetOrAdd(pendingQuestion.QuestionMessage.ContentType, _ => new ConcurrentQueue<PendingQuestion>());

            queue.Enqueue(pendingQuestion);
        }
    }
}
