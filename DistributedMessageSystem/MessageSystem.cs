using NetMQ;
using NetMQ.Sockets;
using Newtonsoft.Json;
using System.Collections.Concurrent;

namespace DistributedMessanger
{
    public class MessageSystem
    {
        private readonly Dictionary<string, string> _systemMap;
        private RouterSocket _routerSocket;
        private NetMQPoller _poller;

        private ConcurrentDictionary<string, MessageBox> _messageBoxes = new ConcurrentDictionary<string, MessageBox>();
        private NetMQQueue<IdentityMessage<AnswerMessage>> _answerQueue = new NetMQQueue<IdentityMessage<AnswerMessage>>();

        public MessageSystem(string address, Dictionary<string, string> systemMap)
        {
            if (address != null)
            {
                _routerSocket = new RouterSocket();
                _routerSocket.Bind(address);

                _poller = new NetMQPoller { _routerSocket, _answerQueue };
                _poller.RunAsync();

                _routerSocket.ReceiveReady += HandleMessage;

                _answerQueue.ReceiveReady += DequeueAndSendAnswer;
            }

            _systemMap = systemMap;
        }

        public MessageBox At(string name)
        {
            return _messageBoxes.GetOrAdd(name,
                _ => new MessageBox(name, _systemMap[name]));
        }

        private void DequeueAndSendAnswer(object? sender, NetMQQueueEventArgs<IdentityMessage<AnswerMessage>> e)
        {
            if (_answerQueue.TryDequeue(out var message, TimeSpan.Zero))
            {
                var messageJson = JsonConvert.SerializeObject(message.Message, Formatting.Indented, new JsonSerializerSettings()
                {
                    TypeNameHandling = TypeNameHandling.None
                });

                _routerSocket.SendMoreFrame(message.DealerIdentity).SendFrame(messageJson);
            }
        }

        private void HandleMessage(object? sender, NetMQSocketEventArgs e)
        {
            var identity = e.Socket.ReceiveFrameString();
            var messageType = Enum.Parse<MessageType>(e.Socket.ReceiveFrameString());
            var messageJson = e.Socket.ReceiveFrameString();

            Message message = DeserializeMessage(messageType, messageJson);

            var messageBox = At(message.MessageBoxName);

            switch (messageType)
            {
                case MessageType.Tell:
                    var answerMessage = (TellMessage)message;
                    messageBox?.WriteMessage(answerMessage);
                    break;

                case MessageType.Question:
                    var questionMessage = (QuestionMessage)message;
                    var pendingQuestion = new PendingQuestion
                    {
                        DealerIdentity = identity,
                        QuestionMessage = questionMessage,
                        AnswerQueue = _answerQueue
                    };

                    messageBox?.WriteQuestion(pendingQuestion);

                    break;
            }

            //messageBox?.WriteMessage(message);
        }

        private Message DeserializeMessage(MessageType messageType, string messageJson)
        {
            switch (messageType)
            {
                case MessageType.Tell:
                    return JsonConvert.DeserializeObject<TellMessage>(messageJson);
                case MessageType.Question:
                    return JsonConvert.DeserializeObject<QuestionMessage>(messageJson);
                default:
                    throw new InvalidOperationException("Unknown message type");
            }
        }
    }
}
