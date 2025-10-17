#include "ZMesh/AbstractMessageBox.hpp"

#include "ZMesh/AnswerMessage.hpp"
#include "ZMesh/Message.hpp"
#include "ZMesh/PendingQuestion.hpp"
#include "ZMesh/QuestionMessage.hpp"
#include "ZMesh/TellMessage.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <iomanip>
#include <future>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <utility>
#include <vector>

namespace zmesh
{
    namespace
    {
        [[nodiscard]] nlohmann::json serialize_message(const Message& message)
        {
            nlohmann::json payload;
            payload["ContentType"] = message.content_type;
            payload["MessageBoxName"] = message.message_box_name;
            if (message.content.has_value())
            {
                payload["Content"] = *message.content;
            }
            else
            {
                payload["Content"] = nullptr;
            }

            return payload;
        }
    }

    AbstractMessageBox::AbstractMessageBox(std::string name, std::string address)
        : name_{std::move(name)}, context_{1}, dealer_socket_{context_, zmq::socket_type::dealer}
    {
        dealer_socket_.set(zmq::sockopt::linger, 0);
        dealer_socket_.set(zmq::sockopt::routing_id, generate_correlation_id());
        dealer_socket_.connect("tcp://" + address);

        receive_thread_ = std::thread(&AbstractMessageBox::receive_loop, this);
    }

    AbstractMessageBox::~AbstractMessageBox()
    {
        *alive_flag_ = false;
        running_.store(false, std::memory_order_release);

        {
            std::scoped_lock socket_lock{socket_mutex_};
            try
            {
                dealer_socket_.close();
            }
            catch (const zmq::error_t&)
            {
                // Socket might already be closed.
            }
        }

        if (receive_thread_.joinable())
        {
            receive_thread_.join();
        }

        std::unordered_map<std::string, PendingAnswerState> pending_answers_copy;
        {
            std::scoped_lock answers_lock{pending_answer_mutex_};
            pending_answers_copy = std::move(pending_answers_);
        }

        for (auto& [_, state] : pending_answers_copy)
        {
            if (state.promise)
            {
                state.promise->set_exception(std::make_exception_ptr(std::runtime_error("Message box disposed")));
            }
        }
    }

    EventConnection AbstractMessageBox::on_question_received(EventHandler handler)
    {
        const auto id = next_event_id_.fetch_add(1, std::memory_order_relaxed);
        {
            std::scoped_lock lock{question_event_mutex_};
            question_handlers_.emplace(id, std::move(handler));
        }

        return EventConnection{
            [this](std::size_t handler_id)
            {
                std::scoped_lock lock{question_event_mutex_};
                question_handlers_.erase(handler_id);
            },
            id,
            alive_flag_};
    }

    EventConnection AbstractMessageBox::on_tell_received(EventHandler handler)
    {
        const auto id = next_event_id_.fetch_add(1, std::memory_order_relaxed);
        {
            std::scoped_lock lock{tell_event_mutex_};
            tell_handlers_.emplace(id, std::move(handler));
        }

        return EventConnection{
            [this](std::size_t handler_id)
            {
                std::scoped_lock lock{tell_event_mutex_};
                tell_handlers_.erase(handler_id);
            },
            id,
            alive_flag_};
    }

    void AbstractMessageBox::tell(const std::string& content_type, const std::string& content)
    {
        TellMessage message;
        message.message_box_name = name_;
        message.content_type = content_type;
        message.content = content;

        auto payload = serialize_message(message);
        send_message(MessageType::Tell, payload);
    }

    bool AbstractMessageBox::try_listen(const std::string& content_type, ContentHandler handler)
    {
        std::optional<std::string> message_content;
        {
            std::scoped_lock lock{message_mutex_};
            auto it = messages_.find(content_type);
            if (it == messages_.end() || it->second.empty())
            {
                return false;
            }

            message_content = std::move(it->second.front());
            it->second.pop();
        }

        if (handler && message_content)
        {
            handler(*message_content);
        }

        return true;
    }

    std::future<Answer> AbstractMessageBox::ask(const std::string& content_type)
    {
        auto result = internal_ask(content_type, std::nullopt);
        return std::move(result.future);
    }

    std::future<Answer> AbstractMessageBox::ask(const std::string& content_type, const std::string& content)
    {
        auto result = internal_ask(content_type, content);
        return std::move(result.future);
    }

    std::future<Answer> AbstractMessageBox::ask(const std::string& content_type, std::stop_token stop_token)
    {
        auto result = internal_ask(content_type, std::nullopt);

        return std::async(std::launch::async,
            [this, correlation_id = std::move(result.correlation_id), future = std::move(result.future), stop_token]() mutable
            {
                if (stop_token.stop_requested())
                {
                    cancel_pending_answer(correlation_id, std::make_exception_ptr(std::runtime_error("Operation canceled")));
                }

                std::stop_callback callback{stop_token, [this, correlation_id]()
                {
                    cancel_pending_answer(correlation_id, std::make_exception_ptr(std::runtime_error("Operation canceled")));
                }};

                return future.get();
            });
    }

    std::future<Answer> AbstractMessageBox::ask(const std::string& content_type, std::chrono::milliseconds timeout)
    {
        return ask_with_timeout(content_type, std::nullopt, timeout);
    }

    std::future<Answer> AbstractMessageBox::ask(const std::string& content_type, const std::string& content, std::stop_token stop_token)
    {
        auto result = internal_ask(content_type, content);

        return std::async(std::launch::async,
            [this, correlation_id = std::move(result.correlation_id), future = std::move(result.future), stop_token]() mutable
            {
                if (stop_token.stop_requested())
                {
                    cancel_pending_answer(correlation_id, std::make_exception_ptr(std::runtime_error("Operation canceled")));
                }

                std::stop_callback callback{stop_token, [this, correlation_id]()
                {
                    cancel_pending_answer(correlation_id, std::make_exception_ptr(std::runtime_error("Operation canceled")));
                }};

                return future.get();
            });
    }

    std::future<Answer> AbstractMessageBox::ask(const std::string& content_type, const std::string& content, std::chrono::milliseconds timeout)
    {
        return ask_with_timeout(content_type, content, timeout);
    }

    bool AbstractMessageBox::try_answer(const std::string& question_content_type, AnswerHandler handler)
    {
        std::shared_ptr<PendingQuestion> pending;
        {
            std::scoped_lock lock{pending_question_mutex_};
            auto it = pending_questions_.find(question_content_type);
            if (it == pending_questions_.end() || it->second.empty())
            {
                return false;
            }

            pending = it->second.front();
            it->second.pop();
        }

        if (!pending)
        {
            return false;
        }

        const auto question_content = pending->question_message().content.value_or(std::string{});
        auto answer = handler ? handler(question_content) : Answer{};
        if (answer.content_type.empty())
        {
            if (const auto& expected = pending->question_message().answer_content_type)
            {
                answer.content_type = *expected;
            }
        }
        pending->answer(answer);
        return true;
    }

    PendingQuestionPtr AbstractMessageBox::get_question(const std::string& question_type, bool& available)
    {
        std::shared_ptr<PendingQuestion> pending;
        {
            std::scoped_lock lock{pending_question_mutex_};
            auto it = pending_questions_.find(question_type);
            if (it == pending_questions_.end() || it->second.empty())
            {
                available = false;
                return nullptr;
            }

            pending = it->second.front();
            it->second.pop();
        }

        available = static_cast<bool>(pending);
        return pending;
    }

    void AbstractMessageBox::receive_loop()
    {
        while (running_.load(std::memory_order_acquire))
        {
            try
            {
                zmq::message_t type_frame;
                zmq::message_t payload_frame;

                if (!dealer_socket_.recv(type_frame, zmq::recv_flags::none))
                {
                    continue;
                }

                if (!dealer_socket_.recv(payload_frame, zmq::recv_flags::none))
                {
                    continue;
                }

                const std::string type_string(static_cast<const char*>(type_frame.data()), type_frame.size());
                const std::string payload_string(static_cast<const char*>(payload_frame.data()), payload_frame.size());

                auto payload = nlohmann::json::parse(payload_string);

                if (type_string == std::string{to_string(MessageType::Tell)})
                {
                    handle_tell_message(payload);
                }
                else if (type_string == std::string{to_string(MessageType::Question)})
                {
                    const auto dealer_identity = payload.value("DealerIdentity", std::string{});
                    handle_question_message(payload, dealer_identity);
                }
                else if (type_string == std::string{to_string(MessageType::Answer)})
                {
                    handle_answer_message(payload);
                }
            }
            catch (const zmq::error_t& error)
            {
                if (!running_.load(std::memory_order_acquire) || error.num() == ETERM)
                {
                    break;
                }
            }
            catch (const std::exception&)
            {
                // Ignore malformed messages but keep processing.
            }
        }
    }

    void AbstractMessageBox::handle_tell_message(const nlohmann::json& payload)
    {
        const auto content_type = payload.value("ContentType", std::string{});
        const auto content = payload.contains("Content") && !payload["Content"].is_null()
            ? payload["Content"].get<std::string>()
            : std::string{};

        {
            std::scoped_lock lock{message_mutex_};
            messages_[content_type].push(content);
        }

        dispatch_event(tell_handlers_, tell_event_mutex_, MessageReceivedEventArgs{content_type});
    }

    void AbstractMessageBox::handle_question_message(const nlohmann::json& payload, std::string dealer_identity)
    {
        QuestionMessage question;
        question.message_box_name = payload.value("MessageBoxName", std::string{});
        question.content_type = payload.value("ContentType", std::string{});
        question.correlation_id = payload.value("CorrelationId", generate_correlation_id());
        if (payload.contains("Content") && !payload["Content"].is_null())
        {
            question.content = payload["Content"].get<std::string>();
        }
        if (payload.contains("AnswerContentType") && !payload["AnswerContentType"].is_null())
        {
            question.answer_content_type = payload["AnswerContentType"].get<std::string>();
        }

        auto pending = std::make_shared<PendingQuestion>(*this, std::move(dealer_identity), question);

        {
            std::scoped_lock lock{pending_question_mutex_};
            if (pending_questions_by_id_.contains(question.correlation_id))
            {
                return;
            }

            pending_questions_by_id_.emplace(question.correlation_id, pending);
            pending_questions_[question.content_type].push(pending);
        }

        dispatch_event(question_handlers_, question_event_mutex_, MessageReceivedEventArgs{question.content_type});
    }

    void AbstractMessageBox::handle_answer_message(const nlohmann::json& payload)
    {
        const auto correlation_id = payload.value("CorrelationId", std::string{});

        std::shared_ptr<std::promise<Answer>> promise;
        {
            std::scoped_lock lock{pending_answer_mutex_};
            auto it = pending_answers_.find(correlation_id);
            if (it == pending_answers_.end())
            {
                return;
            }

            promise = std::move(it->second.promise);
            pending_answers_.erase(it);
        }

        if (!promise)
        {
            return;
        }

        Answer answer;
        answer.content_type = payload.value("ContentType", std::string{});
        if (payload.contains("Content") && !payload["Content"].is_null())
        {
            answer.content = payload["Content"].get<std::string>();
        }

        promise->set_value(std::move(answer));
    }

    void AbstractMessageBox::dispatch_event(std::unordered_map<std::size_t, EventHandler>& handlers, std::mutex& mutex, const MessageReceivedEventArgs& args)
    {
        std::vector<EventHandler> callbacks;
        {
            std::scoped_lock lock{mutex};
            callbacks.reserve(handlers.size());
            for (const auto& [_, handler] : handlers)
            {
                callbacks.push_back(handler);
            }
        }

        for (auto& callback : callbacks)
        {
            if (callback)
            {
                callback(args);
            }
        }
    }

    AbstractMessageBox::InternalAskResult AbstractMessageBox::internal_ask(std::string content_type, std::optional<std::string> content)
    {
        QuestionMessage question;
        question.message_box_name = name_;
        question.content_type = std::move(content_type);
        question.correlation_id = generate_correlation_id();
        question.content = std::move(content);

        auto payload = serialize_message(question);
        payload["CorrelationId"] = question.correlation_id;

        auto promise = std::make_shared<std::promise<Answer>>();
        auto future = promise->get_future();

        {
            std::scoped_lock lock{pending_answer_mutex_};
            pending_answers_.emplace(question.correlation_id, PendingAnswerState{promise});
        }

        send_message(MessageType::Question, payload);

        return InternalAskResult{std::move(future), std::move(question.correlation_id)};
    }

    std::future<Answer> AbstractMessageBox::ask_with_timeout(std::string content_type, std::optional<std::string> content, std::chrono::milliseconds timeout)
    {
        auto result = internal_ask(std::move(content_type), std::move(content));

        return std::async(std::launch::async,
            [this, correlation_id = std::move(result.correlation_id), future = std::move(result.future), timeout]() mutable
            {
                if (future.wait_for(timeout) == std::future_status::ready)
                {
                    return future.get();
                }

                cancel_pending_answer(correlation_id, std::make_exception_ptr(std::timeout_error("Timed out waiting for answer")));
                throw std::timeout_error("Timed out waiting for answer");
            });
    }

    void AbstractMessageBox::cancel_pending_answer(const std::string& correlation_id, std::exception_ptr ex) noexcept
    {
        std::shared_ptr<std::promise<Answer>> promise;
        {
            std::scoped_lock lock{pending_answer_mutex_};
            auto it = pending_answers_.find(correlation_id);
            if (it == pending_answers_.end())
            {
                return;
            }

            promise = std::move(it->second.promise);
            pending_answers_.erase(it);
        }

        if (promise)
        {
            try
            {
                promise->set_exception(std::move(ex));
            }
            catch (...) // promise already satisfied
            {
            }
        }
    }

    void AbstractMessageBox::send_message(MessageType type, const nlohmann::json& payload)
    {
        const auto type_string = to_string(type);
        const auto payload_string = payload.dump();

        std::scoped_lock lock{socket_mutex_};
        if (!running_.load(std::memory_order_acquire))
        {
            throw std::runtime_error("Message box is not running");
        }

        zmq::message_t type_frame{type_string.size()};
        std::memcpy(type_frame.data(), type_string.data(), type_string.size());
        dealer_socket_.send(type_frame, zmq::send_flags::sndmore);

        zmq::message_t payload_frame{payload_string.size()};
        std::memcpy(payload_frame.data(), payload_string.data(), payload_string.size());
        dealer_socket_.send(payload_frame, zmq::send_flags::none);
    }

    void AbstractMessageBox::send_answer(const PendingQuestion& question, const Answer& answer)
    {
        AnswerMessage message;
        message.message_box_name = name_;
        message.content_type = answer.content_type;
        message.content = answer.content;
        message.correlation_id = question.question_message().correlation_id;

        auto payload = serialize_message(message);
        payload["CorrelationId"] = message.correlation_id;
        if (!question.dealer_identity().empty())
        {
            payload["DealerIdentity"] = question.dealer_identity();
        }

        send_message(MessageType::Answer, payload);

        {
            std::scoped_lock lock{pending_question_mutex_};
            pending_questions_by_id_.erase(message.correlation_id);
        }
    }

    std::string AbstractMessageBox::generate_correlation_id() const
    {
        std::array<unsigned char, 16> bytes{};
        std::random_device rd;
        std::mt19937 generator{rd()};
        std::uniform_int_distribution<int> distribution{0, 255};
        for (auto& byte : bytes)
        {
            byte = static_cast<unsigned char>(distribution(generator));
        }

        std::ostringstream stream;
        stream << std::hex << std::setfill('0');
        for (const auto byte : bytes)
        {
            stream << std::setw(2) << static_cast<int>(byte);
        }

        return stream.str();
    }
}
