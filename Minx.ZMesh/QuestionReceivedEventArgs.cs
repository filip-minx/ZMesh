using System;

namespace Minx.ZMesh
{
    public class QuestionReceivedEventArgs : EventArgs
    {
        public string QuestionContentType { get; }

        public QuestionReceivedEventArgs(string questionContentType)
        {
            QuestionContentType = questionContentType;
        }
    }
}
