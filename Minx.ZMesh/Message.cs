namespace Minx.ZMesh
{
    public abstract class Message
    {
        public abstract MessageType MessageType { get; }
        public string ContentType { get; set; }
        public string Content { get; set; }
        public string MessageBoxName { get; set; }
    }
}
