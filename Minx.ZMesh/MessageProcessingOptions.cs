using Minx.ZMesh.Models;
using System;

namespace Minx.ZMesh
{
    public class MessageProcessingOptions
    {
        public Action<(MessageType messageType, string contentType)> OnMissingHandler { get; set; } = null;
    }
}
