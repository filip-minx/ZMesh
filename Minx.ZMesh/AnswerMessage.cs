namespace Minx.ZMesh
{
    public class AnswerMessage : Message
    {
        public override MessageType MessageType => MessageType.Answer;
        public string CorrelationId { get; set; }
    }
}
