using System;

namespace Minx.ZMesh
{
    public class QuestionReceivedEventArgs : EventArgs
    {
        public string QuestionContentType { get; }
        public string AnswerContentType { get; }

        public QuestionReceivedEventArgs(string questionContentType, string answerContentType)
        {
            QuestionContentType = questionContentType;
            AnswerContentType = answerContentType;
        }
    }
}
