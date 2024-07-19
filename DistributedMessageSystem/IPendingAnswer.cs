namespace DistributedMessanger
{
    public interface IPendingAnswer
    {
        string CorrelationId { get; }

        void SetAnswer(object answer);

        Type GetAnswerType();
    }
}
