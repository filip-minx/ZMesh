﻿using System.Threading.Tasks;

namespace Minx.ZMesh
{
    public class PendingAnswer : IPendingAnswer
    {
        public string CorrelationId { get; set; }

        public TaskCompletionSource<string> TaskCompletionSource { get; set; }


        public void SetAnswer(string answer)
        {
            TaskCompletionSource.SetResult(answer);
        }

        public void Cancel()
        {
            TaskCompletionSource.SetCanceled();
        }
    }
}