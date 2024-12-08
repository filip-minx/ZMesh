using System;
using System.Threading;
using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public interface IAbstractMessageBox
    {
        event EventHandler<MessageReceivedEventArgs> QuestionReceived;
        event EventHandler<MessageReceivedEventArgs> TellReceived;

        void Tell(string contentType, string content);
        bool TryListen(string contentType, Action<string> handler);
        Task<Answer> Ask(string contentType, string content);
        bool TryAnswer(string questionContentType, Func<string, Answer> handler);

        Task<Answer> Ask(string contentType);
        Task<Answer> Ask(string contentType, CancellationToken cancellationToken);
        Task<Answer> Ask(string contentType, TimeSpan timeout);
        Task<Answer> Ask(string contentType, string content, CancellationToken cancellationToken);
        Task<Answer> Ask(string contentType, string content, TimeSpan timeout);

        IPendingQuestion GetQuestion(string questionType, out bool available);
    }
}