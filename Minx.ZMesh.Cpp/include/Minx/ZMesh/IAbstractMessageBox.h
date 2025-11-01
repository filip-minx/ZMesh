#pragma once

#include "Answer.h"
#include "MessageReceivedEventArgs.h"
#include "PendingQuestion.h"

#include <chrono>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace Minx::ZMesh
{
    class IAbstractMessageBox
    {
    public:
        using QuestionReceivedHandler = std::function<void(const MessageReceivedEventArgs&)>;
        using TellReceivedHandler = std::function<void(const MessageReceivedEventArgs&)>;
        using ListenHandler = std::function<void(const std::string&)>;
        using AnswerHandler = std::function<Answer(const std::string&)>;

        virtual ~IAbstractMessageBox() = default;

        virtual void OnQuestionReceived(QuestionReceivedHandler handler) = 0;
        virtual void OnTellReceived(TellReceivedHandler handler) = 0;

        virtual void Tell(std::string_view content_type, std::string_view content) = 0;
        virtual bool TryListen(std::string content_type, ListenHandler handler) = 0;
        virtual Answer Ask(std::string_view content_type) = 0;
        virtual Answer Ask(std::string_view content_type, std::string_view content) = 0;
        virtual Answer Ask(std::string_view content_type, std::string_view content, std::chrono::milliseconds timeout) = 0;
        virtual Answer Ask(std::string_view content_type, std::string_view content, std::stop_token stop_token) = 0;
        virtual bool TryAnswer(std::string question_content_type, AnswerHandler handler) = 0;

        virtual std::optional<PendingQuestion> GetQuestion(std::string question_type) = 0;
    };
}

