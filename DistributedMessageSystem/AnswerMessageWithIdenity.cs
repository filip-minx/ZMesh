namespace DistributedMessanger
{
    public class IdentityMessage<TMessage>
    {
        public TMessage Message { get; set; }
        public string DealerIdentity { get; set; }
    }
}
