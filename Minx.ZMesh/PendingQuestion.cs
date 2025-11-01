using Minx.ZMesh.Models;
using Minx.ZMesh.Serialization;
using NetMQ;

namespace Minx.ZMesh
{
    public class PendingQuestion : IPendingQuestion
    {
        private string messageBoxName;

        public string DealerIdentity { get; set; }

        public QuestionMessage QuestionMessage { get; set; }

        public NetMQQueue<IdentityMessage<AnswerMessage>> AnswerQueue { get; set; }

        public PendingQuestion(string messageBoxName)
        {
            this.messageBoxName = messageBoxName;
        }

        public void Answer(Answer answer)
        {
            var answerMessage = new AnswerMessage
            {
                ContentType = answer.ContentType,
                Content = answer.Content,
                MessageBoxName = messageBoxName,
                CorrelationId = QuestionMessage.CorrelationId
            };

            var answerWithIdentity = new IdentityMessage<AnswerMessage>
            {
                Message = answerMessage,
                DealerIdentity = DealerIdentity
            };

            AnswerQueue.Enqueue(answerWithIdentity);
        }
    }
}
