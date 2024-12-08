namespace Minx.ZMesh
{
    public interface IPendingAnswer
    {
        string CorrelationId { get; }

        void SetAnswer(Answer answer);

        void Cancel();
    }
}
