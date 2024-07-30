namespace Minx.ZMesh
{
    public class QuestionMessage : Message
    {
        public override MessageType MessageType => MessageType.Question;
        public string CorrelationId { get; set; }

        public string AnswerContentType { get; set; }
    }
}
