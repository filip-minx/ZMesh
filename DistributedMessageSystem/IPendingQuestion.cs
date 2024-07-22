namespace DistributedMessanger
{
    public interface IPendingQuestion
    {
        string DealerIdentity { get; set; }

        QuestionMessage QuestionMessage { get; set; }
        void Answer(object answer);
    }
}
