using System;
using System.Collections.Generic;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public class MessageBoxProcessor : IDisposable
    {
        private readonly MessageBox messageBox;

        private Dictionary<string, Action<object>> tellHandlers = new Dictionary<string, Action<object>>();
        private Dictionary<string, Func<object, object>> questionHandlers = new Dictionary<string, Func<object, object>>();

        private bool isDisposed;
        private readonly Channel<EventArgs> eventChannel;

        public MessageBoxProcessor(MessageBox messageBox)
        {
            this.messageBox = messageBox;

            eventChannel = Channel.CreateUnbounded<EventArgs>();

            messageBox.TellReceived += MessageBox_TellReceived;
            messageBox.QuestionReceived += MessageBox_QuestionReceived;
        }

        private void MessageBox_QuestionReceived(object sender, QuestionReceivedEventArgs e)
        {
            QueueEvent(e);
        }

        private void MessageBox_TellReceived(object sender, MessageReceivedEventArgs e)
        {
            QueueEvent(e);
        }

        private void QueueEvent(EventArgs e)
        {
            if (!isDisposed)
            {
                eventChannel.Writer.TryWrite(e);
            }
        }

        public void ProcessOne()
        {
            if (eventChannel.Reader.TryRead(out var eventArgs))
            {
                HandleEvent(eventArgs);
            }
        }

        public async Task ProcessAll()
        {
            while (await eventChannel.Reader.WaitToReadAsync())
            {
                while (eventChannel.Reader.TryRead(out var eventArgs))
                {
                    HandleEvent(eventArgs);
                }
            }
        }

        private void HandleEvent(EventArgs eventArgs)
        {
            switch (eventArgs)
            {
                case MessageReceivedEventArgs messageArgs:
                    OnMessageReceived(this, messageArgs);
                    break;

                case QuestionReceivedEventArgs questionArgs:
                    OnQuestionReceived(this, questionArgs);
                    break;
            }

        }

        private void OnMessageReceived(object sender, MessageReceivedEventArgs e)
        {
            messageBox.TryListen(e.ContentType, tellHandlers[e.ContentType]);
        }

        private void OnQuestionReceived(object sender, QuestionReceivedEventArgs e)
        {
            messageBox.TryAnswer(e.QuestionContentType, questionHandlers[e.QuestionContentType]);
        }

        public void Listen<TMessage>(Action<TMessage> handler)
        {
            tellHandlers[typeof(TMessage).Name] = message => handler((TMessage)message);
        }

        public void Answer<TQuestion, TAnswer>(Func<TQuestion, TAnswer> handler)
        {
            questionHandlers[typeof(TQuestion).Name] = message => handler((TQuestion)message);
        }

        public void Dispose()
        {
            messageBox.TellReceived -= MessageBox_TellReceived;
            messageBox.QuestionReceived -= MessageBox_QuestionReceived;

            isDisposed = true;
            eventChannel.Writer.Complete();
        }
    }
}
