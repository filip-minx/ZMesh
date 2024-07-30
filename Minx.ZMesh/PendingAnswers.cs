using System;
using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public class PendingAnswers<TAnswer> : IPendingAnswer
    {
        public string CorrelationId { get; set; }

        public TaskCompletionSource<TAnswer> TaskCompletionSource { get; set; }

        public Type GetAnswerType()
        {
            return typeof(TAnswer);
        }

        public void SetAnswer(TAnswer answer)
        {
            TaskCompletionSource.SetResult(answer);
        }

        public void SetAnswer(object answer)
        {
            TaskCompletionSource.SetResult((TAnswer)answer);
        }
    }
}
