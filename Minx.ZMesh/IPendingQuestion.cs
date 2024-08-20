using Minx.ZMesh.Models;

namespace Minx.ZMesh
{
    public interface IPendingQuestion
    {
        string DealerIdentity { get; set; }

        QuestionMessage QuestionMessage { get; set; }

        void Answer(object answer);
    }
}
