#pragma once

#include "Answer.hpp"
#include "MessageReceivedEventArgs.hpp"
#include "PendingQuestion.hpp"

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stop_token>
#include <string>

namespace zmesh
{
    class EventConnection
    {
    public:
        EventConnection() = default;
        EventConnection(std::function<void(std::size_t)> unsubscribe, std::size_t id, std::weak_ptr<bool> alive_flag) noexcept
            : unsubscribe_(std::move(unsubscribe)), id_(id), alive_flag_(std::move(alive_flag))
        {
        }

        EventConnection(const EventConnection&) = delete;
        EventConnection& operator=(const EventConnection&) = delete;

        EventConnection(EventConnection&& other) noexcept
            : unsubscribe_(std::move(other.unsubscribe_)), id_(other.id_), alive_flag_(std::move(other.alive_flag_))
        {
            other.id_ = 0U;
        }

        EventConnection& operator=(EventConnection&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                unsubscribe_ = std::move(other.unsubscribe_);
                id_ = other.id_;
                alive_flag_ = std::move(other.alive_flag_);
                other.id_ = 0U;
            }
            return *this;
        }

        ~EventConnection()
        {
            reset();
        }

        void reset()
        {
            if (unsubscribe_ && id_ != 0U)
            {
                if (auto alive = alive_flag_.lock())
                {
                    if (*alive)
                    {
                        unsubscribe_(id_);
                    }
                }
            }
            unsubscribe_ = nullptr;
            id_ = 0U;
            alive_flag_.reset();
        }

        explicit operator bool() const noexcept
        {
            return static_cast<bool>(unsubscribe_);
        }

    private:
        std::function<void(std::size_t)> unsubscribe_;
        std::size_t id_{};
        std::weak_ptr<bool> alive_flag_;
    };

    class IAbstractMessageBox
    {
    public:
        using EventHandler = std::function<void(const MessageReceivedEventArgs&)>;
        using ContentHandler = std::function<void(const std::string&)>;
        using AnswerHandler = std::function<Answer(const std::string&)>;

        virtual ~IAbstractMessageBox() = default;

        [[nodiscard]] virtual EventConnection on_question_received(EventHandler handler) = 0;
        [[nodiscard]] virtual EventConnection on_tell_received(EventHandler handler) = 0;

        virtual void tell(const std::string& content_type, const std::string& content) = 0;
        virtual bool try_listen(const std::string& content_type, ContentHandler handler) = 0;
        virtual std::future<Answer> ask(const std::string& content_type) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, const std::string& content) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, std::stop_token stop_token) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, std::chrono::milliseconds timeout) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, const std::string& content, std::stop_token stop_token) = 0;
        virtual std::future<Answer> ask(const std::string& content_type, const std::string& content, std::chrono::milliseconds timeout) = 0;
        virtual bool try_answer(const std::string& question_content_type, AnswerHandler handler) = 0;
        virtual PendingQuestionPtr get_question(const std::string& question_type, bool& available) = 0;
    };
}
