namespace Minx.ZMesh.Models
{
    public class IdentityMessage<TMessage> where TMessage : Message
    {
        public TMessage Message { get; set; }
        public string DealerIdentity { get; set; }
    }
}
