namespace Minx.ZMesh
{
    public class IdentityMessage<TMessage>
    {
        public TMessage Message { get; set; }
        public string DealerIdentity { get; set; }
    }

    //public class AnswerMessageWithIdenity
    //{
    //    public AnswerMessage AnswerMessage { get; set; }
    //    public string DealerIdentity { get; set; }
    //}
}
