using System;

namespace Minx.ZMesh
{
    public interface IPendingAnswer
    {
        string CorrelationId { get; }

        void SetAnswer(object answer);

        Type GetAnswerType();
    }
}
