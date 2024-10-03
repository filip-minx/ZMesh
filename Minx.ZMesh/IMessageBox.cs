using System;
using System.Threading;
using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public interface IMessageBox
    {
        event EventHandler<MessageReceivedEventArgs> QuestionReceived;
        event EventHandler<MessageReceivedEventArgs> TellReceived;

        Task<string> Ask(string contentType);
        Task<string> Ask(string contentType, CancellationToken cancellationToken);
        Task<string> Ask(string contentType, TimeSpan timeout);

        Task<string> Ask(string contentType, string content);
        Task<string> Ask(string contentType, string content, CancellationToken cancellationToken);
        Task<string> Ask(string contentType, string content, TimeSpan timeout);

        Task<TAnswer> Ask<TQuestion, TAnswer>() where TQuestion : new();
        Task<TAnswer> Ask<TQuestion, TAnswer>(CancellationToken cancellationToken) where TQuestion : new();
        Task<TAnswer> Ask<TQuestion, TAnswer>(TimeSpan timeout) where TQuestion : new();

        Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question);
        Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question, CancellationToken cancellationToken);
        Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question, TimeSpan timeout);

        void Tell(string contentType, string content);
        void Tell<TMessage>(TMessage message);

        bool TryAnswer<TQuestion, TAnswer>(Func<TQuestion, TAnswer> handler);
        bool TryAnswer(string questionContentType, Func<object, object> handler);

        bool TryListen<TMessage>(Action<TMessage> handler);
        bool TryListen(string contentType, Action<object> handler);

        IPendingQuestion GetQuestion(string questionType, out bool available);
    }
}