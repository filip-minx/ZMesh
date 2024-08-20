using System;
using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public interface IMessageBoxProcessor
    {
        /// <summary>
        /// Registers a handler function for answering a question of type <typeparamref name="TQuestion"/> with a result of type <typeparamref name="TAnswer"/>.
        /// </summary>
        /// <typeparam name="TQuestion">The type of the question.</typeparam>
        /// <typeparam name="TAnswer">The type of the answer.</typeparam>
        /// <param name="handler">The handler function that takes a question of type <typeparamref name="TQuestion"/> and returns an answer of type <typeparamref name="TAnswer"/>.</param>
        void Answer<TQuestion, TAnswer>(Func<TQuestion, TAnswer> handler);

        /// <summary>
        /// Registers a handler action for listening to messages of type <typeparamref name="TMessage"/>.
        /// </summary>
        /// <typeparam name="TMessage">The type of the message.</typeparam>
        /// <param name="handler">The handler action that takes a message of type <typeparamref name="TMessage"/>.</param>
        void Listen<TMessage>(Action<TMessage> handler);

        /// <summary>
        /// Processes all messages asynchronously.
        /// </summary>
        /// <returns>A task representing the asynchronous operation.</returns>
        Task ProcessAll();

        /// <summary>
        /// Synchronously processes one received message if available. The message handler is invoked on the callee thread.
        /// </summary>
        void ProcessOne();
    }
}