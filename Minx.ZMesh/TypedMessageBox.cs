using Minx.ZMesh.Serialization;
using System;
using System.Threading;
using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public class TypedMessageBox : AbstractMessageBox, ITypedMessageBox
    {
        private readonly ISerializer serializer;

        public TypedMessageBox(string name, string address, ISerializer serializer) :base(name, address)
        {
            this.serializer = serializer;
        }

        private async Task<TAnswer> InternalAsk<TQuestion, TAnswer>(TQuestion question, CancellationToken cancellationToken)
        {
            var serializedQuestion = serializer.Serialize(question);

            var answer = await Ask(typeof(TQuestion).Name, serializedQuestion, cancellationToken)
                .ConfigureAwait(false);

            return serializer.Deserialize<TAnswer>(answer.Content);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>() where TQuestion : new()
        {
            return await InternalAsk<TQuestion, TAnswer>(new TQuestion(), CancellationToken.None)
                .ConfigureAwait(false);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(CancellationToken cancellationToken) where TQuestion : new()
        {
            return await InternalAsk<TQuestion, TAnswer>(new TQuestion(), cancellationToken)
                .ConfigureAwait(false);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TimeSpan timeout) where TQuestion : new()
        {
            using (var cts = new CancellationTokenSource(timeout))
            {
                return await InternalAsk<TQuestion, TAnswer>(new TQuestion(), cts.Token)
                    .ConfigureAwait(false);
            }
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question)
        {
            return await InternalAsk<TQuestion, TAnswer>(question, CancellationToken.None)
                .ConfigureAwait(false);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question, CancellationToken cancellationToken)
        {
            return await InternalAsk<TQuestion, TAnswer>(question, cancellationToken)
                .ConfigureAwait(false);
        }

        public async Task<TAnswer> Ask<TQuestion, TAnswer>(TQuestion question, TimeSpan timeout)
        {
            using (var cts = new CancellationTokenSource(timeout))
            {
                return await InternalAsk<TQuestion, TAnswer>(question, cts.Token)
                    .ConfigureAwait(false);
            }
        }

        public void Tell<TMessage>(TMessage message)
        {
            Tell(typeof(TMessage).Name, serializer.Serialize(message));
        }

        public bool TryAnswer<TQuestion, TAnswer>(Func<TQuestion, TAnswer> handler)
        {
            return TryAnswer(typeof(TQuestion).Name, questionJson =>
            {
                var question = serializer.Deserialize<TQuestion>(questionJson);

                var answer = handler(question);

                return new Answer
                {
                    ContentType = typeof(TAnswer).Name,
                    Content = serializer.Serialize(answer)
                };
            });
        }

        public bool TryAnswerGeneric(string questionContentType, Func<object, object> handler)
        {
            return TryAnswer(questionContentType, serializedQuestion =>
            {
                var question = serializer.Deserialize(serializedQuestion, TypeResolver.GetTypeInAllAssemblies(questionContentType));

                var answer = handler(question);

                return new Answer
                {
                    ContentType = answer.GetType().Name,
                    Content = serializer.Serialize(answer)
                };
            });
        }

        public bool TryListen<TMessage>(Action<TMessage> handler)
        {
            return TryListen(typeof(TMessage).Name, serializedMessage =>
            {
                var message = serializer.Deserialize<TMessage>(serializedMessage);

                handler(message);
            });
        }

        public bool TryListenGeneric(string contentType, Action<object> handler)
        {
            return TryListen(contentType, serializedMessage =>
            {
                var message = serializer.Deserialize(serializedMessage, TypeResolver.GetTypeInAllAssemblies(contentType));

                handler(message);
            });
        }
    }
}
