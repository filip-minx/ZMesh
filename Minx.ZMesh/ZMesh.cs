using Minx.ZMesh.Models;
using Minx.ZMesh.Serialization;
using NetMQ;
using NetMQ.Sockets;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using JsonSerializer = Minx.ZMesh.Serialization.JsonSerializer;

namespace Minx.ZMesh
{
    public class ZMesh : IZMesh, IDisposable
    {
        public ISerializer Serializer { get; set; } = new JsonSerializer();

        private readonly Dictionary<string, string> _systemMap;
        private RouterSocket _routerSocket;
        private NetMQPoller _poller;

        private ConcurrentDictionary<string, TypedMessageBox> _messageBoxes = new ConcurrentDictionary<string, TypedMessageBox>();
        private NetMQQueue<IdentityMessage<AnswerMessage>> _answerQueue = new NetMQQueue<IdentityMessage<AnswerMessage>>();

        public ZMesh(string address, Dictionary<string, string> systemMap)
        {
            _systemMap = systemMap;

            if (address != null)
            {
                _routerSocket = new RouterSocket();
                _routerSocket.Bind("tcp://" + address);

                _poller = new NetMQPoller { _routerSocket, _answerQueue };

                _routerSocket.ReceiveReady += HandleMessage;

                _answerQueue.ReceiveReady += DequeueAndSendAnswer;

                _poller.RunAsync();
            }
        }

        public ITypedMessageBox At(string name)
        {
            return _messageBoxes.GetOrAdd(name,
                _ => new TypedMessageBox(name, _systemMap[name], Serializer));
        }

        private void DequeueAndSendAnswer(object sender, NetMQQueueEventArgs<IdentityMessage<AnswerMessage>> e)
        {
            if (_answerQueue.TryDequeue(out var message, TimeSpan.Zero))
            {
                _routerSocket.SendMoreFrame(message.DealerIdentity)
                    .SendMoreFrame(message.Message.MessageType.ToString())
                    .SendMoreFrame(message.Message.MessageBoxName ?? string.Empty)
                    .SendMoreFrame(message.Message.ContentType ?? string.Empty)
                    .SendMoreFrame(message.Message.CorrelationId ?? string.Empty)
                    .SendFrame(message.Message.Content ?? string.Empty);
            }
        }

        private void HandleMessage(object sender, NetMQSocketEventArgs e)
        {
            var identity = e.Socket.ReceiveFrameString();
            var messageType = (MessageType)Enum.Parse(typeof(MessageType), e.Socket.ReceiveFrameString());
            var messageBoxName = e.Socket.ReceiveFrameString();
            var contentType = e.Socket.ReceiveFrameString();
            var correlationId = e.Socket.ReceiveFrameString();
            var content = e.Socket.ReceiveFrameString();

            var messageBox = (TypedMessageBox)At(messageBoxName);

            switch (messageType)
            {
                case MessageType.Tell:
                    var tellMessage = new TellMessage
                    {
                        MessageBoxName = messageBoxName,
                        ContentType = contentType,
                        Content = content
                    };

                    messageBox?.WriteTellMessage(tellMessage);
                    break;

                case MessageType.Question:
                    var questionMessage = new QuestionMessage
                    {
                        MessageBoxName = messageBoxName,
                        ContentType = contentType,
                        Content = content,
                        CorrelationId = correlationId
                    };
                    var pendingQuestion = new PendingQuestion(questionMessage.MessageBoxName)
                    {
                        DealerIdentity = identity,
                        QuestionMessage = questionMessage,
                        AnswerQueue = _answerQueue
                    };

                    messageBox?.WriteQuestionMessage(pendingQuestion);

                    break;
            }
        }

        public void Dispose()
        {
            _poller?.Dispose();
            _routerSocket?.Dispose();

            foreach (var mb in _messageBoxes.Values)
            {
                mb.Dispose();
            }

            _poller = null;
            _routerSocket = null;
        }
    }
}
