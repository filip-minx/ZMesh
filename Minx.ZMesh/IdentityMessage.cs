namespace Minx.ZMesh
{
    public class IdentityMessage<TMessage> where TMessage : Message
    {
        public TMessage Message { get; set; }
        public string DealerIdentity { get; set; }
    }
}
