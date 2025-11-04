#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <string>

#include "pending_question.hpp"
#include "types.hpp"

namespace minx::zmesh {

class IAbstractMessageBox {
public:
    using TellHandler = std::function<void(const std::string&)>;
    using QuestionHandler = std::function<Answer(const std::string&)>;

    virtual ~IAbstractMessageBox() = default;

    virtual void Tell(std::string content_type, std::string content) = 0;
    virtual bool TryListen(const std::string& content_type, const TellHandler& handler) = 0;

    virtual std::future<Answer> Ask(const std::string& content_type) = 0;
    virtual std::future<Answer> Ask(const std::string& content_type, std::string content) = 0;
    virtual std::future<Answer> Ask(const std::string& content_type, std::chrono::milliseconds timeout) = 0;
    virtual std::future<Answer> Ask(const std::string& content_type, std::string content, std::chrono::milliseconds timeout) = 0;

    virtual bool TryAnswer(const std::string& question_content_type, const QuestionHandler& handler) = 0;

    virtual std::optional<PendingQuestion> GetQuestion(const std::string& question_type) = 0;
};

} // namespace minx::zmesh
