using System;
using System.Threading;
using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public interface ITypedMessageBox : IAbstractMessageBox
    {
        Task<TAnswer> Ask<TQuestion, TAnswer>() where TQuestion : new();
        Task<TAnswer> Ask<TQuestion, TAnswer>(CancellationToken cancellationToken) where TQuestion : new();
        Task<TAnswer> Ask<TQuestion, TAnswer>(TimeSpan timeout) where TQuestion : new();

        Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question);
        Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question, CancellationToken cancellationToken);
        Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question, TimeSpan timeout);

        void Tell<TMessage>(TMessage message);

        bool TryAnswer<TQuestion, TAnswer>(Func<TQuestion, TAnswer> handler);
        bool TryAnswerGeneric(string questionContentType, Func<object, object> handler);

        bool TryListen<TMessage>(Action<TMessage> handler);
        bool TryListenGeneric(string contentType, Action<object> handler);
    }
}