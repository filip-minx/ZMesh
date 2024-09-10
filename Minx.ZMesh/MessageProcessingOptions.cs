using Minx.ZMesh.Models;
using System;

namespace Minx.ZMesh
{
    public class MessageProcessingOptions
    {
        /// <summary>
        /// When set, this action will be called when a message is received that has no handler.
        /// </summary>
        public Action<MessageType, string> OnMissingHandler { get; set; } = null;

        /// <summary>
        /// When set, this action will be called when an unhandled exception occurs during message processing.
        /// </summary>
        public Action<Exception> OnUnhandledException { get; set; } = null;
    }
}
