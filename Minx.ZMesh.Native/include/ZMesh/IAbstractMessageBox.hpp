#pragma once

#include "Answer.hpp"
#include "MessageReceivedEventArgs.hpp"
#include "PendingQuestion.hpp"
#include "Signal.hpp"

#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <stop_token>
#include <string>

namespace zmesh
{
    class IAbstractMessageBox
    {
    public:
        using MessageReceivedHandler = std::function<void(const MessageReceivedEventArgs&)>;
        using Subscription = Signal<MessageReceivedEventArgs>::Subscription;

        virtual ~IAbstractMessageBox() = default;

        virtual Subscription on_question_received(MessageReceivedHandler handler) = 0;
        virtual Subscription on_tell_received(MessageReceivedHandler handler) = 0;

        virtual void tell(const std::string& content_type, const std::string& content) = 0;
        virtual bool try_listen(const std::string& content_type, std::function<void(const std::string&)> handler) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, const std::string& content) = 0;
        virtual bool try_answer(const std::string& question_content_type, std::function<Answer(const std::string&)> handler) = 0;

        virtual std::future<Answer> ask(const std::string& content_type) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, std::chrono::milliseconds timeout) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, const std::string& content, std::chrono::milliseconds timeout) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, std::stop_token cancellation_token) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, const std::string& content, std::stop_token cancellation_token) = 0;

        virtual std::optional<PendingQuestion> get_question(const std::string& question_type, bool& available) = 0;
    };
}
