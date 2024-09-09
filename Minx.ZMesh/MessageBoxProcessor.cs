using Minx.ZMesh.Models;
using System;
using System.Collections.Generic;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public class MessageBoxProcessor : IMessageBoxProcessor, IDisposable
    {
        private readonly IMessageBox messageBox;
        private readonly MessageProcessingOptions options;
        private readonly Dictionary<string, Action<object>> tellHandlers = new Dictionary<string, Action<object>>();
        private readonly Dictionary<string, Func<object, object>> questionHandlers = new Dictionary<string, Func<object, object>>();
        private readonly Channel<(MessageType messageType, string contentType)> messageChannel;

        private bool isDisposed;

        public MessageBoxProcessor(IMessageBox messageBox, MessageProcessingOptions options = null)
        {
            this.messageBox = messageBox;
            this.options = options ?? new MessageProcessingOptions();

            messageChannel = Channel.CreateUnbounded<(MessageType, string)>();

            messageBox.TellReceived += MessageBox_TellReceived;
            messageBox.QuestionReceived += MessageBox_QuestionReceived;
        }

        private void MessageBox_QuestionReceived(object sender, MessageReceivedEventArgs e)
        {
            QueueMessageChannelItem(e.ContentType, MessageType.Question);
        }

        private void MessageBox_TellReceived(object sender, MessageReceivedEventArgs e)
        {
            QueueMessageChannelItem(e.ContentType, MessageType.Tell);
        }

        private void QueueMessageChannelItem(string contentType, MessageType messageType)
        {
            if (!isDisposed)
            {
                messageChannel.Writer.TryWrite((messageType, contentType));
            }
        }

        public void ProcessOne()
        {
            if (messageChannel.Reader.TryRead(out var item))
            {
                HandleMessage(item.messageType, item.contentType);
            }
        }

        public async Task ProcessAll()
        {
            while (await messageChannel.Reader.WaitToReadAsync())
            {
                while (messageChannel.Reader.TryRead(out var item))
                {
                    HandleMessage(item.messageType, item.contentType);
                }
            }
        }

        private void HandleMessage(MessageType messageType, string contentType)
        {
            switch (messageType)
            {
                case MessageType.Tell:
                    HandleTellMessage(contentType);
                    break;

                case MessageType.Question:
                    HandleQuestionMessage(contentType);
                    break;

                default:
                    throw new InvalidOperationException($"Unexpected message type: {messageType}");
            }
        }

        private void HandleQuestionMessage(string contentType)
        {
            if (questionHandlers.TryGetValue(contentType, out var handler))
            {
                messageBox.TryAnswer(contentType, handler);
            }
            else
            {
                if (options.OnMissingHandler == null)
                {
                    throw new InvalidOperationException($"No handler for message type: {contentType}");
                }

                options.OnMissingHandler?.Invoke((MessageType.Question, contentType));
            }
        }

        private void HandleTellMessage(string contentType)
        {
            if (tellHandlers.TryGetValue(contentType, out var handler))
            {
                messageBox.TryListen(contentType, handler);
            }
            else
            {
                if (options.OnMissingHandler == null)
                {
                    throw new InvalidOperationException($"No handler for message type: {contentType}");
                }

                options.OnMissingHandler?.Invoke((MessageType.Tell, contentType));
            }
        }

        public void Listen<TMessage>(Action<TMessage> handler)
        {
            ThrowIfArgumentIsNull(handler, nameof(handler));

            tellHandlers[typeof(TMessage).Name] = message => handler((TMessage)message);
        }

        public void Answer<TQuestion, TAnswer>(Func<TQuestion, TAnswer> handler)
        {
            ThrowIfArgumentIsNull(handler, nameof(handler));

            questionHandlers[typeof(TQuestion).Name] = message => handler((TQuestion)message);
        }

        private static void ThrowIfArgumentIsNull(object argumentValue, string argumentName)
        {
            if (argumentValue == null)
            {
                throw new ArgumentNullException(argumentName);
            }
        }

        public void Dispose()
        {
            messageBox.TellReceived -= MessageBox_TellReceived;
            messageBox.QuestionReceived -= MessageBox_QuestionReceived;

            isDisposed = true;
            messageChannel.Writer.Complete();
        }
    }
}
