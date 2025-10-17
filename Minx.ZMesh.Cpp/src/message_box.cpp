#include "zmesh/message_box.hpp"

#include <cstring>
#include <stdexcept>
#include <string_view>

#include "zmesh/uuid.hpp"

namespace zmesh {

namespace {
constexpr auto ANSWER_CACHE_TTL = std::chrono::minutes(1);
constexpr auto POLL_INTERVAL = std::chrono::milliseconds(50);
}

PendingQuestion::PendingQuestion(MessageBox& owner, QuestionMessage question, std::string dealer_identity)
    : owner_{&owner},
      question_{std::move(question)},
      dealer_identity_{std::move(dealer_identity)} {}

const QuestionMessage& PendingQuestion::question() const noexcept { return question_; }

const std::string& PendingQuestion::dealer_identity() const noexcept { return dealer_identity_; }

void PendingQuestion::answer(const Answer& answer) { owner_->send_answer(*this, answer); }

MessageBox::MessageBox(std::string name,
                       std::string endpoint,
                       zmq::context_t& context,
                       std::function<void(const std::string&, AnswerMessage)> answer_sender)
    : name_{std::move(name)},
      endpoint_{std::move(endpoint)},
      context_{context},
      identity_{uuid::generate()},
      answer_sender_{std::move(answer_sender)},
      worker_{[this](std::stop_token stop_token) { worker_loop(std::move(stop_token)); }} {}

MessageBox::~MessageBox() {
    if (worker_.joinable()) {
        worker_.request_stop();
        outgoing_cv_.notify_all();
        worker_.join();
    }

    std::lock_guard lock(pending_answers_mutex_);
    for (auto& [id, promise] : pending_answers_) {
        if (promise) {
            promise->set_exception(std::make_exception_ptr(std::runtime_error("message box shutting down")));
        }
    }
    pending_answers_.clear();
}

Answer MessageBox::ask(const std::string& content_type,
                       std::optional<std::string> content,
                       RequestOptions options) {
    if (options.max_retries <= 0) {
        throw std::invalid_argument("max_retries must be greater than zero");
    }

    QuestionMessage question;
    question.content_type = content_type;
    question.content = content.value_or(std::string{});
    question.message_box_name = name_;
    question.correlation_id = uuid::generate();

    const auto question_payload = serialize_question_message(question);

    auto promise = std::make_shared<std::promise<Answer>>();
    auto future = promise->get_future();

    {
        std::lock_guard lock(pending_answers_mutex_);
        pending_answers_.emplace(question.correlation_id, promise);
    }

    for (int attempt = 0; attempt < options.max_retries; ++attempt) {
        enqueue_outgoing(MessageType::Question, question_payload);

        if (future.wait_for(options.timeout) == std::future_status::ready) {
            return future.get();
        }
    }

    {
        std::lock_guard lock(pending_answers_mutex_);
        pending_answers_.erase(question.correlation_id);
    }

    throw std::runtime_error("ZMesh request timed out after " + std::to_string(options.max_retries) + " attempts");
}

Answer MessageBox::ask(const std::string& content_type, RequestOptions options) {
    return ask(content_type, std::nullopt, options);
}

Answer MessageBox::ask(const std::string& content_type, std::chrono::milliseconds timeout) {
    RequestOptions options;
    options.timeout = timeout;
    return ask(content_type, std::nullopt, options);
}

Answer MessageBox::ask(const std::string& content_type,
                       const std::string& content,
                       std::chrono::milliseconds timeout) {
    RequestOptions options;
    options.timeout = timeout;
    return ask(content_type, content, options);
}

void MessageBox::tell(const std::string& content_type, const std::string& content) {
    TellMessage message;
    message.content_type = content_type;
    message.content = content;
    message.message_box_name = name_;

    enqueue_outgoing(MessageType::Tell, serialize_tell_message(message));
}

bool MessageBox::try_listen(const std::string& content_type,
                            const std::function<void(const std::string&)>& handler) {
    std::string message;

    {
        std::lock_guard lock(tells_mutex_);
        auto it = tell_messages_.find(content_type);
        if (it == tell_messages_.end() || it->second.empty()) {
            return false;
        }

        message = std::move(it->second.front());
        it->second.pop();

        if (it->second.empty()) {
            tell_messages_.erase(it);
        }
    }

    handler(message);
    return true;
}

bool MessageBox::try_answer(const std::string& question_content_type,
                            const std::function<Answer(const std::string&)>& handler) {
    std::shared_ptr<PendingQuestion> pending_question;

    {
        std::lock_guard lock(pending_questions_mutex_);
        auto it = pending_questions_.find(question_content_type);
        if (it == pending_questions_.end() || it->second.empty()) {
            return false;
        }

        pending_question = it->second.front();
        it->second.pop();
        if (it->second.empty()) {
            pending_questions_.erase(it);
        }
    }

    const auto& question_message = pending_question->question();
    const auto correlation_id = question_message.correlation_id;
    auto answer = handler(question_message.content);

    cache_answer(correlation_id, answer);
    pending_question->answer(answer);

    return true;
}

std::shared_ptr<PendingQuestion> MessageBox::get_question(const std::string& question_type, bool& available) {
    std::shared_ptr<PendingQuestion> pending_question;

    {
        std::lock_guard lock(pending_questions_mutex_);
        auto it = pending_questions_.find(question_type);
        if (it != pending_questions_.end() && !it->second.empty()) {
            pending_question = it->second.front();
            it->second.pop();
            if (it->second.empty()) {
                pending_questions_.erase(it);
            }
        }
    }

    available = pending_question != nullptr;
    return pending_question;
}

size_t MessageBox::add_tell_received_handler(TellHandler handler) {
    const auto token = ++next_handler_token_;
    {
        std::lock_guard lock(tell_handlers_mutex_);
        tell_handlers_.emplace(token, std::move(handler));
    }
    return token;
}

void MessageBox::remove_tell_received_handler(size_t token) {
    std::lock_guard lock(tell_handlers_mutex_);
    tell_handlers_.erase(token);
}

size_t MessageBox::add_question_received_handler(QuestionHandler handler) {
    const auto token = ++next_handler_token_;
    {
        std::lock_guard lock(question_handlers_mutex_);
        question_handlers_.emplace(token, std::move(handler));
    }
    return token;
}

void MessageBox::remove_question_received_handler(size_t token) {
    std::lock_guard lock(question_handlers_mutex_);
    question_handlers_.erase(token);
}

void MessageBox::worker_loop(std::stop_token stop_token) {
    zmq::socket_t socket{context_, zmq::socket_type::dealer};
    socket.set(zmq::sockopt::rcvtimeo, 0);
    socket.set(zmq::sockopt::linger, 0);
    socket.set(zmq::sockopt::routing_id, identity_);
    socket.connect("tcp://" + endpoint_);

    while (!stop_token.stop_requested()) {
        {
            std::unique_lock lock(outgoing_mutex_);
            outgoing_cv_.wait_for(lock, POLL_INTERVAL, [&] {
                return stop_token.stop_requested() || !outgoing_messages_.empty();
            });

            while (!outgoing_messages_.empty()) {
                auto message = std::move(outgoing_messages_.front());
                outgoing_messages_.pop_front();
                lock.unlock();
                send_message(socket, message);
                lock.lock();
            }
        }

        zmq::pollitem_t items[] = {{static_cast<void*>(socket), 0, ZMQ_POLLIN, 0}};
        [[maybe_unused]] const auto poll_result = zmq::poll(items, 1, POLL_INTERVAL);

        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t payload_frame;
            if (!socket.recv(payload_frame, zmq::recv_flags::none)) {
                continue;
            }

            const auto payload = std::string_view(static_cast<const char*>(payload_frame.data()), payload_frame.size());
            auto answer = deserialize_answer_message(payload);
            handle_answer(answer);
        }
    }
}

void MessageBox::send_message(zmq::socket_t& socket, const OutgoingMessage& message) {
    const auto& payload = message.payload;
    const auto type_string = std::string{to_string(message.type)};

    zmq::message_t type_frame{type_string.begin(), type_string.end()};
    zmq::message_t payload_frame{payload.size()};
    if (!payload.empty()) {
        std::memcpy(payload_frame.data(), payload.data(), payload.size());
    }

    const auto type_sent = socket.send(type_frame, zmq::send_flags::sndmore);
    if (!type_sent) {
        throw std::runtime_error("failed to send message type frame");
    }

    const auto payload_sent = socket.send(payload_frame, zmq::send_flags::none);
    if (!payload_sent) {
        throw std::runtime_error("failed to send message payload frame");
    }
}

void MessageBox::handle_answer(const AnswerMessage& answer) {
    std::shared_ptr<std::promise<Answer>> promise;

    {
        std::lock_guard lock(pending_answers_mutex_);
        auto it = pending_answers_.find(answer.correlation_id);
        if (it != pending_answers_.end()) {
            promise = std::move(it->second);
            pending_answers_.erase(it);
        }
    }

    if (promise) {
        promise->set_value(Answer{answer.content_type, answer.content});
    }
}

void MessageBox::enqueue_outgoing(MessageType type, std::string payload) {
    {
        std::lock_guard lock(outgoing_mutex_);
        outgoing_messages_.push_back(OutgoingMessage{type, std::move(payload)});
    }
    outgoing_cv_.notify_one();
}

void MessageBox::prune_expired_cache() {
    std::vector<std::string> expired_ids;
    const auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard lock(cache_mutex_);
        for (auto it = response_cache_.begin(); it != response_cache_.end();) {
            if (it->second.expires_at <= now) {
                expired_ids.push_back(it->first);
                it = response_cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!expired_ids.empty()) {
        std::lock_guard lock(seen_questions_mutex_);
        for (const auto& id : expired_ids) {
            seen_question_ids_.erase(id);
        }
    }
}

std::optional<Answer> MessageBox::get_cached_answer(const std::string& correlation_id) {
    std::lock_guard lock(cache_mutex_);
    if (auto it = response_cache_.find(correlation_id); it != response_cache_.end()) {
        if (it->second.expires_at > std::chrono::steady_clock::now()) {
            return it->second.answer;
        }
    }
    return std::nullopt;
}

void MessageBox::cache_answer(const std::string& correlation_id, const Answer& answer) {
    const auto expires_at = std::chrono::steady_clock::now() + ANSWER_CACHE_TTL;
    std::lock_guard lock(cache_mutex_);
    response_cache_[correlation_id] = CachedAnswer{answer, expires_at};
}

void MessageBox::send_answer(const PendingQuestion& pending_question, const Answer& answer) {
    if (!answer_sender_) {
        throw std::runtime_error("This message box is not configured to send answers");
    }

    AnswerMessage message;
    message.content_type = answer.content_type;
    message.content = answer.content;
    message.message_box_name = name_;
    message.correlation_id = pending_question.question().correlation_id;

    answer_sender_(pending_question.dealer_identity(), std::move(message));
}

void MessageBox::write_tell_message(const TellMessage& message) {
    {
        std::lock_guard lock(tells_mutex_);
        tell_messages_[message.content_type].push(message.content);
    }

    notify_tell_received(message.content_type);
}

void MessageBox::write_question_message(const QuestionMessage& message, const std::string& dealer_identity) {
    prune_expired_cache();

    auto pending_question = std::make_shared<PendingQuestion>(*this, message, dealer_identity);
    const auto correlation_id = message.correlation_id;

    bool inserted = false;
    {
        std::lock_guard lock(seen_questions_mutex_);
        inserted = seen_question_ids_.insert(correlation_id).second;
    }

    if (inserted) {
        {
            std::lock_guard lock(pending_questions_mutex_);
            pending_questions_[message.content_type].push(pending_question);
        }

        notify_question_received(message.content_type);
    } else if (auto cached = get_cached_answer(correlation_id)) {
        pending_question->answer(*cached);
    }
}

void MessageBox::notify_tell_received(const std::string& content_type) {
    std::vector<TellHandler> handlers;
    {
        std::lock_guard lock(tell_handlers_mutex_);
        for (const auto& [_, handler] : tell_handlers_) {
            handlers.push_back(handler);
        }
    }

    MessageReceivedEventArgs args{content_type};
    for (auto& handler : handlers) {
        handler(args);
    }
}

void MessageBox::notify_question_received(const std::string& content_type) {
    std::vector<QuestionHandler> handlers;
    {
        std::lock_guard lock(question_handlers_mutex_);
        for (const auto& [_, handler] : question_handlers_) {
            handlers.push_back(handler);
        }
    }

    MessageReceivedEventArgs args{content_type};
    for (auto& handler : handlers) {
        handler(args);
    }
}

} // namespace zmesh

