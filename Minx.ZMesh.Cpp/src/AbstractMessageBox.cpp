#include "Minx/ZMesh/AbstractMessageBox.h"

#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

using namespace std::chrono_literals;

namespace Minx::ZMesh
{
    constexpr std::string_view AbstractMessageBox::ToMessageTypeString(MessageType type) noexcept
    {
        switch (type)
        {
        case MessageType::Tell:
            return "Tell";
        case MessageType::Question:
            return "Question";
        case MessageType::Answer:
            return "Answer";
        }
        return "";
    }

    AbstractMessageBox::MessageType AbstractMessageBox::ParseMessageType(const std::string_view value)
    {
        if (value == "Tell")
        {
            return MessageType::Tell;
        }

        if (value == "Question")
        {
            return MessageType::Question;
        }

        if (value == "Answer")
        {
            return MessageType::Answer;
        }

        throw std::runtime_error{"Unsupported message type received."};
    }

    AbstractMessageBox::AbstractMessageBox(std::string endpoint, ConnectionMode mode)
        : endpoint_{std::move(endpoint)}
        , mode_{mode}
        , context_{1}
        , socket_{context_, zmq::socket_type::pair}
        , random_engine_{std::random_device{}()}
    {
        socket_.set(zmq::sockopt::rcvtimeo, 100);
        socket_.set(zmq::sockopt::sndtimeo, 100);
        socket_.set(zmq::sockopt::linger, 0);

        if (mode_ == ConnectionMode::Bind)
        {
            socket_.bind(endpoint_);
        }
        else
        {
            socket_.connect(endpoint_);
        }

        receiver_thread_ = std::jthread{[this](std::stop_token token) { RunReceiver(token); }};
    }

    AbstractMessageBox::~AbstractMessageBox()
    {
        receiver_thread_.request_stop();
        receiver_thread_.join();
        socket_.close();
    }

    void AbstractMessageBox::OnQuestionReceived(QuestionReceivedHandler handler)
    {
        std::lock_guard lock{event_mutex_};
        question_handlers_.push_back(std::move(handler));
    }

    void AbstractMessageBox::OnTellReceived(TellReceivedHandler handler)
    {
        std::lock_guard lock{event_mutex_};
        tell_handlers_.push_back(std::move(handler));
    }

    void AbstractMessageBox::Tell(std::string_view content_type, std::string_view content)
    {
        SendMessage(MessageType::Tell, content_type, {}, content);
    }

    bool AbstractMessageBox::TryListen(std::string content_type, ListenHandler handler)
    {
        std::lock_guard lock{listen_mutex_};
        return listen_handlers_.emplace(std::move(content_type), std::move(handler)).second;
    }

    MessageResponse AbstractMessageBox::Ask(std::string_view content_type)
    {
        return Ask(content_type, "");
    }

    MessageResponse AbstractMessageBox::Ask(std::string_view content_type, std::string_view content)
    {
        const auto correlation_id = NextCorrelationId();
        auto future = [&]() {
            std::promise<MessageResponse> promise;
            auto future_value = promise.get_future();
            {
                std::lock_guard pending_lock{pending_answers_mutex_};
                pending_answers_.emplace(correlation_id, std::move(promise));
            }
            return future_value;
        }();

        SendMessage(MessageType::Question, content_type, correlation_id, content);
        return future.get();
    }

    MessageResponse AbstractMessageBox::Ask(std::string_view content_type,
                                            std::string_view content,
                                            std::chrono::milliseconds timeout)
    {
        const auto correlation_id = NextCorrelationId();
        auto future = [&]() {
            std::promise<MessageResponse> promise;
            auto future_value = promise.get_future();
            {
                std::lock_guard pending_lock{pending_answers_mutex_};
                pending_answers_.emplace(correlation_id, std::move(promise));
            }
            return future_value;
        }();

        SendMessage(MessageType::Question, content_type, correlation_id, content);

        if (future.wait_for(timeout) != std::future_status::ready)
        {
            {
                std::lock_guard pending_lock{pending_answers_mutex_};
                if (auto it = pending_answers_.find(correlation_id); it != pending_answers_.end())
                {
                    it->second.set_exception(std::make_exception_ptr(std::runtime_error{"Ask timed out."}));
                    pending_answers_.erase(it);
                }
            }

            throw std::runtime_error{"Ask timed out."};
        }

        return future.get();
    }

    MessageResponse AbstractMessageBox::Ask(std::string_view content_type,
                                            std::string_view content,
                                            std::stop_token stop_token)
    {
        const auto correlation_id = NextCorrelationId();
        auto future = [&]() {
            std::promise<MessageResponse> promise;
            auto future_value = promise.get_future();
            {
                std::lock_guard pending_lock{pending_answers_mutex_};
                pending_answers_.emplace(correlation_id, std::move(promise));
            }
            return future_value;
        }();

        SendMessage(MessageType::Question, content_type, correlation_id, content);

        while (!stop_token.stop_requested())
        {
            if (future.wait_for(50ms) == std::future_status::ready)
            {
                return future.get();
            }
        }

        {
            std::lock_guard pending_lock{pending_answers_mutex_};
            if (auto it = pending_answers_.find(correlation_id); it != pending_answers_.end())
            {
                it->second.set_exception(std::make_exception_ptr(std::runtime_error{"Ask cancelled."}));
                pending_answers_.erase(it);
            }
        }

        throw std::runtime_error{"Ask cancelled."};
    }

    bool AbstractMessageBox::TryAnswer(std::string question_content_type, AnswerHandler handler)
    {
        std::lock_guard lock{listen_mutex_};
        return answer_handlers_.emplace(std::move(question_content_type), std::move(handler)).second;
    }

    std::optional<PendingQuestion> AbstractMessageBox::GetQuestion(std::string question_type)
    {
        std::lock_guard lock{pending_questions_mutex_};
        if (auto it = pending_questions_.find(question_type); it != pending_questions_.end())
        {
            if (!it->second.empty())
            {
                auto pending = it->second.front();
                it->second.pop();
                return pending;
            }
        }

        return std::nullopt;
    }

    void AbstractMessageBox::RunReceiver(std::stop_token stop_token)
    {
        try
        {
            while (!stop_token.stop_requested())
            {
                zmq::message_t type_frame;
                if (!socket_.recv(type_frame, zmq::recv_flags::none))
                {
                    if (zmq_errno() == EAGAIN)
                    {
                        continue;
                    }

                    if (stop_token.stop_requested())
                    {
                        break;
                    }

                    continue;
                }

                zmq::message_t content_type_frame;
                zmq::message_t correlation_id_frame;
                zmq::message_t payload_frame;

                if (!socket_.recv(content_type_frame, zmq::recv_flags::none) ||
                    !socket_.recv(correlation_id_frame, zmq::recv_flags::none) ||
                    !socket_.recv(payload_frame, zmq::recv_flags::none))
                {
                    continue;
                }

                const auto message_type = ParseMessageType(type_frame.to_string_view());
                const std::string content_type{content_type_frame.to_string_view()};
                const std::string correlation_id{correlation_id_frame.to_string_view()};
                const std::string content{payload_frame.to_string_view()};

                switch (message_type)
                {
                case MessageType::Tell:
                    DispatchTell(content_type, content);
                    break;
                case MessageType::Question:
                    DispatchQuestion(correlation_id, content_type, content);
                    break;
                case MessageType::Answer:
                    DispatchAnswer(correlation_id, content_type, content);
                    break;
                }
            }
        }
        catch (const zmq::error_t& ex)
        {
            if (ex.num() == ETERM || ex.num() == EINTR)
            {
                return;
            }

            throw;
        }
    }

    void AbstractMessageBox::DispatchTell(const std::string& content_type, const std::string& content)
    {
        std::vector<TellReceivedHandler> tell_handlers;
        {
            std::lock_guard lock{event_mutex_};
            tell_handlers = tell_handlers_;
        }

        MessageReceivedEventArgs args{content_type};
        for (auto& handler : tell_handlers)
        {
            if (handler)
            {
                handler(args);
            }
        }

        ListenHandler handler;
        {
            std::lock_guard lock{listen_mutex_};
            if (auto it = listen_handlers_.find(content_type); it != listen_handlers_.end())
            {
                handler = it->second;
            }
        }

        if (handler)
        {
            handler(content);
        }
    }

    void AbstractMessageBox::DispatchQuestion(const std::string& correlation_id,
                                              const std::string& content_type,
                                              const std::string& content)
    {
        std::vector<QuestionReceivedHandler> question_handlers;
        {
            std::lock_guard lock{event_mutex_};
            question_handlers = question_handlers_;
        }

        MessageReceivedEventArgs args{content_type};
        for (auto& handler : question_handlers)
        {
            if (handler)
            {
                handler(args);
            }
        }

        AnswerHandler handler;
        {
            std::lock_guard lock{listen_mutex_};
            if (auto it = answer_handlers_.find(content_type); it != answer_handlers_.end())
            {
                handler = it->second;
            }
        }

        if (handler)
        {
            const auto answer = handler(content);
            SendAnswer(correlation_id, answer);
            return;
        }

        PendingQuestion pending{weak_from_this(), correlation_id, content_type, content};
        {
            std::lock_guard lock{pending_questions_mutex_};
            pending_questions_[content_type].push(std::move(pending));
        }
    }

    void AbstractMessageBox::DispatchAnswer(const std::string& correlation_id,
                                            const std::string& content_type,
                                            const std::string& content)
    {
        std::promise<MessageResponse> promise;
        {
            std::lock_guard lock{pending_answers_mutex_};
            if (auto it = pending_answers_.find(correlation_id); it != pending_answers_.end())
            {
                promise = std::move(it->second);
                pending_answers_.erase(it);
            }
            else
            {
                return;
            }
        }

        promise.set_value(MessageResponse{content_type, content});
    }

    void AbstractMessageBox::SendMessage(MessageType type,
                                         std::string_view content_type,
                                         std::string_view correlation_id,
                                         std::string_view content)
    {
        const auto message_type = ToMessageTypeString(type);

        zmq::message_t type_frame{message_type.size()};
        std::memcpy(type_frame.data(), message_type.data(), message_type.size());

        zmq::message_t content_type_frame{content_type.size()};
        std::memcpy(content_type_frame.data(), content_type.data(), content_type.size());

        zmq::message_t correlation_frame{correlation_id.size()};
        std::memcpy(correlation_frame.data(), correlation_id.data(), correlation_id.size());

        zmq::message_t payload_frame{content.size()};
        std::memcpy(payload_frame.data(), content.data(), content.size());

        socket_.send(type_frame, zmq::send_flags::sndmore);
        socket_.send(content_type_frame, zmq::send_flags::sndmore);
        socket_.send(correlation_frame, zmq::send_flags::sndmore);
        socket_.send(payload_frame, zmq::send_flags::none);
    }

    void AbstractMessageBox::SendAnswer(const std::string& correlation_id, const MessageResponse& response) const
    {
        const_cast<AbstractMessageBox*>(this)->SendMessage(MessageType::Answer,
                                                          response.content_type,
                                                          correlation_id,
                                                          response.content);
    }

    std::string AbstractMessageBox::NextCorrelationId()
    {
        std::lock_guard lock{correlation_mutex_};
        std::array<char, 32> buffer{};
        std::uniform_int_distribution<int> distribution{0, 15};
        for (auto& ch : buffer)
        {
            const auto value = distribution(random_engine_);
            ch = static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10));
        }
        return {buffer.data(), buffer.size()};
    }
}

