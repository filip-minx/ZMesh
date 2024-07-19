using NetMQ;

namespace DistributedMessanger
{
    class PendingQuestion
    {
        public string DealerIdentity { get; set; }

        public QuestionMessage QuestionMessage { get; set; }

        public NetMQQueue<IdentityMessage<AnswerMessage>> AnswerQueue { get; set; }
    }
}
