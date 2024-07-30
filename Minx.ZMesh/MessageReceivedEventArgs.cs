using System;

namespace Minx.ZMesh
{
    public class MessageReceivedEventArgs : EventArgs
    {
        public string ContentType { get; }

        public MessageReceivedEventArgs(string contentType)
        {
            ContentType = contentType;
        }
    }
}
