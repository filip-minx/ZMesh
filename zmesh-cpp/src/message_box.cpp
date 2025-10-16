#include "zmesh/message_box.hpp"

#include "zmesh/messages.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace zmesh
{
    namespace
    {
        std::string generate_random_identity()
        {
            std::random_device rd;
            std::mt19937_64 engine(rd());
            std::uniform_int_distribution<uint64_t> dist;

            std::ostringstream oss;
            oss << std::hex << dist(engine) << dist(engine);
            return oss.str();
        }
    }

    MessageBox::MessageBox(std::string name,
                           std::string address,
                           std::shared_ptr<zmq::context_t> context)
        : name_(std::move(name)),
          address_(std::move(address)),
          context_(std::move(context)),
          dealer_(*context_, zmq::socket_type::dealer)
    {
        auto identity = generate_random_identity();
        dealer_.set(zmq::sockopt::routing_id, identity);
        dealer_.connect("tcp://" + address_);

        running_.store(true, std::memory_order_release);
        worker_ = std::thread(&MessageBox::worker_loop, this);
    }

    MessageBox::~MessageBox()
    {
        running_.store(false, std::memory_order_release);
        outgoing_cv_.notify_all();

        try
        {
            dealer_.close();
        }
        catch (const zmq::error_t&)
        {
        }

        if (worker_.joinable())
        {
            worker_.join();
        }

        std::lock_guard answers_lock(pending_answers_mutex_);
        for (auto& entry : pending_answers_)
        {
            if (entry.second)
            {
                entry.second->set_exception(std::make_exception_ptr(std::runtime_error("MessageBox disposed")));
            }
        }
        pending_answers_.clear();
    }

    void MessageBox::tell(const std::string& content_type, const std::string& content)
    {
        TellMessage message;
        message.content_type = content_type;
        message.content = content;
        message.message_box_name = name_;
        enqueue_outbound(MessageType::Tell, tell_message_to_json(message));
    }

    bool MessageBox::try_listen(const std::string& content_type, const std::function<void(const std::string&)>& handler)
    {
        std::string payload;
        {
            std::lock_guard lock(tell_mutex_);
            auto& queue = tell_messages_[content_type];
            if (queue.empty())
            {
                return false;
            }

            payload = std::move(queue.front());
            queue.pop();
        }

        handler(payload);
        return true;
    }

    Answer MessageBox::ask(const std::string& content_type,
                           std::optional<std::string> content,
                           std::chrono::milliseconds timeout)
    {
        constexpr int max_retries = 3;
        constexpr auto retry_timeout = std::chrono::milliseconds(3000);

        QuestionMessage question;
        question.message_box_name = name_;
        question.content_type = content_type;
        question.content = std::move(content);
        question.correlation_id = generate_correlation_id();

        auto promise = std::make_shared<std::promise<Answer>>();
        auto future = promise->get_future();

        {
            std::lock_guard lock(pending_answers_mutex_);
            pending_answers_.emplace(question.correlation_id, promise);
        }

        auto payload = question_message_to_json(question);
        std::chrono::milliseconds waited{0};

        for (int attempt = 0; attempt < max_retries; ++attempt)
        {
            enqueue_outbound(MessageType::Question, payload);

            auto wait_duration = retry_timeout;
            if (timeout.count() > 0)
            {
                auto remaining = timeout - waited;
                if (remaining <= std::chrono::milliseconds::zero())
                {
                    break;
                }

                wait_duration = std::min(wait_duration, remaining);
            }

            if (future.wait_for(wait_duration) == std::future_status::ready)
            {
                try
                {
                    return future.get();
                }
                catch (...)
                {
                    std::lock_guard lock(pending_answers_mutex_);
                    pending_answers_.erase(question.correlation_id);
                    throw;
                }
            }

            if (timeout.count() > 0)
            {
                waited += wait_duration;
            }
        }

        {
            std::lock_guard lock(pending_answers_mutex_);
            pending_answers_.erase(question.correlation_id);
        }

        throw std::runtime_error("Failed to receive a response before timeout");
    }

    std::future<Answer> MessageBox::async_ask(const std::string& content_type, std::optional<std::string> content)
    {
        QuestionMessage question;
        question.message_box_name = name_;
        question.content_type = content_type;
        question.content = std::move(content);
        question.correlation_id = generate_correlation_id();

        auto promise = std::make_shared<std::promise<Answer>>();
        auto future = promise->get_future();

        {
            std::lock_guard lock(pending_answers_mutex_);
            pending_answers_.emplace(question.correlation_id, promise);
        }

        enqueue_outbound(MessageType::Question, question_message_to_json(question));
        return future;
    }

    bool MessageBox::try_answer(const std::string& question_content_type,
                                const std::function<Answer(const std::optional<std::string>&)>& handler)
    {
        cleanup_cache();

        PendingQuestionPtr pending_question;
        {
            std::lock_guard lock(pending_questions_mutex_);
            auto& queue = pending_questions_by_type_[question_content_type];
            if (queue.empty())
            {
                return false;
            }

            pending_question = queue.front();
            queue.pop_front();
        }

        if (!pending_question)
        {
            return false;
        }

        Answer answer = handler(pending_question->question_message.content);
        cache_answer(pending_question->question_message.correlation_id, answer);
        pending_question->answer(answer);
        return true;
    }

    PendingQuestionPtr MessageBox::get_question(const std::string& question_content_type, bool& available)
    {
        cleanup_cache();

        std::lock_guard lock(pending_questions_mutex_);
        auto& queue = pending_questions_by_type_[question_content_type];
        if (queue.empty())
        {
            available = false;
            return nullptr;
        }

        available = true;
        auto pending_question = queue.front();
        queue.pop_front();
        return pending_question;
    }

    void MessageBox::on_tell_received(MessageReceivedHandler handler)
    {
        std::lock_guard lock(tell_handlers_mutex_);
        tell_handlers_.push_back(std::move(handler));
    }

    void MessageBox::on_question_received(MessageReceivedHandler handler)
    {
        std::lock_guard lock(question_handlers_mutex_);
        question_handlers_.push_back(std::move(handler));
    }

    void MessageBox::write_tell_message(const TellMessage& message)
    {
        {
            std::lock_guard lock(tell_mutex_);
            tell_messages_[message.content_type].push(message.content.value_or(std::string{}));
        }

        std::vector<MessageReceivedHandler> handlers;
        {
            std::lock_guard lock(tell_handlers_mutex_);
            handlers = tell_handlers_;
        }

        for (auto& handler : handlers)
        {
            handler(message.content_type);
        }
    }

    void MessageBox::write_question_message(PendingQuestionPtr pending_question)
    {
        cleanup_cache();

        bool inserted = false;
        {
            std::lock_guard lock(pending_questions_by_id_mutex_);
            auto result = pending_questions_by_id_.emplace(pending_question->question_message.correlation_id, pending_question);
            inserted = result.second;
        }

        if (!inserted)
        {
            Answer cached_answer;
            if (try_get_cached_answer(pending_question->question_message.correlation_id, cached_answer))
            {
                pending_question->answer(cached_answer);
            }
            return;
        }

        {
            std::lock_guard lock(pending_questions_mutex_);
            pending_questions_by_type_[pending_question->question_message.content_type].push_back(pending_question);
        }

        std::vector<MessageReceivedHandler> handlers;
        {
            std::lock_guard lock(question_handlers_mutex_);
            handlers = question_handlers_;
        }

        for (auto& handler : handlers)
        {
            handler(pending_question->question_message.content_type);
        }
    }

    void MessageBox::handle_answer_message(const AnswerMessage& message)
    {
        std::shared_ptr<std::promise<Answer>> pending_answer;
        {
            std::lock_guard lock(pending_answers_mutex_);
            auto it = pending_answers_.find(message.correlation_id);
            if (it == pending_answers_.end())
            {
                return;
            }

            pending_answer = it->second;
            pending_answers_.erase(it);
        }

        if (pending_answer)
        {
            pending_answer->set_value(Answer{message.content_type, message.content.value_or(std::string{})});
        }
    }

    void MessageBox::enqueue_outbound(MessageType message_type, const nlohmann::json& payload)
    {
        OutboundMessage message{message_type, payload.dump()};
        {
            std::lock_guard lock(outgoing_mutex_);
            outgoing_messages_.push(std::move(message));
        }
        outgoing_cv_.notify_one();
    }

    void MessageBox::worker_loop()
    {
        while (running_.load(std::memory_order_acquire))
        {
            flush_outgoing();
            process_incoming();

            std::unique_lock lock(outgoing_mutex_);
            outgoing_cv_.wait_for(lock, std::chrono::milliseconds(50), [this]
                                  {
                                      return !outgoing_messages_.empty() || !running_.load(std::memory_order_acquire);
                                  });
        }

        flush_outgoing();
        process_incoming();
    }

    void MessageBox::flush_outgoing()
    {
        std::queue<OutboundMessage> messages;
        {
            std::lock_guard lock(outgoing_mutex_);
            std::swap(messages, outgoing_messages_);
        }

        while (!messages.empty())
        {
            auto message = std::move(messages.front());
            messages.pop();

            auto type_string = to_string(message.type);
            zmq::message_t type_frame(type_string.size());
            std::memcpy(type_frame.data(), type_string.data(), type_string.size());
            dealer_.send(type_frame, zmq::send_flags::sndmore);

            zmq::message_t payload_frame(message.payload.size());
            std::memcpy(payload_frame.data(), message.payload.data(), message.payload.size());
            dealer_.send(payload_frame, zmq::send_flags::none);
        }
    }

    void MessageBox::process_incoming()
    {
        while (true)
        {
            zmq::message_t payload;
            auto result = dealer_.recv(payload, zmq::recv_flags::dontwait);
            if (!result)
            {
                break;
            }

            auto json = nlohmann::json::parse(payload.to_string());
            auto answer = answer_message_from_json(json);
            handle_answer_message(answer);
        }
    }

    std::string MessageBox::generate_correlation_id() const
    {
        return generate_random_identity();
    }

    void MessageBox::cache_answer(const std::string& correlation_id, const Answer& answer)
    {
        cleanup_cache();

        CachedAnswer cache_entry{answer, std::chrono::steady_clock::now() + std::chrono::minutes(1)};
        std::lock_guard lock(cache_mutex_);
        response_cache_[correlation_id] = std::move(cache_entry);
    }

    bool MessageBox::try_get_cached_answer(const std::string& correlation_id, Answer& answer)
    {
        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard lock(cache_mutex_);
            auto it = response_cache_.find(correlation_id);
            if (it == response_cache_.end())
            {
                return false;
            }

            if (it->second.expires_at <= now)
            {
                response_cache_.erase(it);
                return false;
            }

            answer = it->second.answer;
            return true;
        }
    }

    void MessageBox::cleanup_cache()
    {
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> expired;

        {
            std::lock_guard lock(cache_mutex_);
            for (auto it = response_cache_.begin(); it != response_cache_.end();)
            {
                if (it->second.expires_at <= now)
                {
                    expired.push_back(it->first);
                    it = response_cache_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        if (!expired.empty())
        {
            std::lock_guard lock(pending_questions_by_id_mutex_);
            for (const auto& key : expired)
            {
                pending_questions_by_id_.erase(key);
            }
        }
    }
}

