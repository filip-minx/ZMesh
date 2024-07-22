using NetMQ;
using Newtonsoft.Json;
using System.Xml.Linq;

namespace DistributedMessanger
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

        public void Answer(object answer)
        {
            var questionMessage = JsonConvert.DeserializeObject(QuestionMessage.Content, TypeResolver.GetTypeInAllAssemblies(QuestionMessage.ContentType));

            var answerContentJson = JsonConvert.SerializeObject(answer, Formatting.Indented, new JsonSerializerSettings()
            {
                TypeNameHandling = TypeNameHandling.None
            });

            var answerMessage = new AnswerMessage
            {
                ContentType = answer.GetType().Name,
                Content = answerContentJson,
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
