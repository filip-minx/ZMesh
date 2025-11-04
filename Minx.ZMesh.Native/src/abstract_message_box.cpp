#include "minx/zmesh/abstract_message_box.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

#include "minx/zmesh/pending_question.hpp"
#include "minx/zmesh/types.hpp"

namespace minx::zmesh {

AbstractMessageBox::AbstractMessageBox(std::string name,
                                       std::string address,
                                       zmq::context_t& context,
                                       std::shared_ptr<AnswerQueue> answer_queue)
    : name_(std::move(name)),
      address_(std::move(address)),
      context_(context),
      answer_queue_(std::move(answer_queue)),
      dealer_(context_, zmq::socket_type::dealer) {
    dealer_.set(zmq::sockopt::linger, 0);

    std::random_device rd;
    {
        std::lock_guard random_lock(random_mutex_);
        random_engine_.seed(rd());
    }

    dealer_.set(zmq::sockopt::routing_id, GenerateCorrelationId());

    dealer_.connect("tcp://" + address_);

    dealer_thread_ = std::jthread([this](std::stop_token stop_token) { DealerLoop(stop_token); });
}

AbstractMessageBox::~AbstractMessageBox() {
    outgoing_messages_.close();

    if (dealer_thread_.joinable()) {
        dealer_thread_.request_stop();
        dealer_thread_.join();
    }

    try {
        dealer_.close();
    } catch (...) {
    }

    std::lock_guard lock(pending_answers_mutex_);
    for (auto& [id, promise] : pending_answers_) {
        try {
            throw std::runtime_error("Message box disposed");
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    }
    pending_answers_.clear();
}

void AbstractMessageBox::Tell(std::string content_type, std::string content) {
    outgoing_messages_.push(TellMessage{.message_box_name = name_,
                                        .content_type = std::move(content_type),
                                        .content = std::move(content)});
}

bool AbstractMessageBox::TryListen(const std::string& content_type, const TellHandler& handler) {
    auto queue = GetOrCreateMessageQueue(content_type);
    std::string message;
    if (!queue->try_pop(message)) {
        return false;
    }
    handler(message);
    return true;
}

std::future<Answer> AbstractMessageBox::Ask(const std::string& content_type) {
    return InternalAsk(content_type, std::nullopt, std::nullopt);
}

std::future<Answer> AbstractMessageBox::Ask(const std::string& content_type, std::string content) {
    return InternalAsk(content_type, std::move(content), std::nullopt);
}

std::future<Answer> AbstractMessageBox::Ask(const std::string& content_type, std::chrono::milliseconds timeout) {
    return InternalAsk(content_type, std::nullopt, timeout);
}

std::future<Answer> AbstractMessageBox::Ask(const std::string& content_type,
                                            std::string content,
                                            std::chrono::milliseconds timeout) {
    return InternalAsk(content_type, std::move(content), timeout);
}

bool AbstractMessageBox::TryAnswer(const std::string& question_content_type, const QuestionHandler& handler) {
    auto queue = GetOrCreatePendingQueue(question_content_type);
    PendingQuestion pending_question;
    if (!queue->try_pop(pending_question)) {
        return false;
    }

    Answer answer = handler(pending_question.question_message.content);
    SendAnswer(pending_question, answer);

    return true;
}

std::optional<PendingQuestion> AbstractMessageBox::GetQuestion(const std::string& question_type) {
    auto queue = GetOrCreatePendingQueue(question_type);
    PendingQuestion pending_question;
    if (!queue->try_pop(pending_question)) {
        return std::nullopt;
    }
    return pending_question;
}

void AbstractMessageBox::ReceiveTell(const TellMessage& message) {
    auto queue = GetOrCreateMessageQueue(message.content_type);
    queue->push(message.content);
}

void AbstractMessageBox::ReceiveQuestion(const PendingQuestion& pending_question) {
    auto queue = GetOrCreatePendingQueue(pending_question.question_message.content_type);
    queue->push(pending_question);
}

void AbstractMessageBox::ReceiveAnswer(const AnswerMessage& message) {
    FulfillPendingAnswer(message.correlation_id, Answer{message.content_type, message.content});
}

std::shared_ptr<ThreadSafeQueue<std::string>>
AbstractMessageBox::GetOrCreateMessageQueue(const std::string& content_type) {
    std::lock_guard lock(messages_mutex_);
    auto it = messages_.find(content_type);
    if (it == messages_.end()) {
        auto queue = std::make_shared<ThreadSafeQueue<std::string>>();
        it = messages_.emplace(content_type, std::move(queue)).first;
    }
    return it->second;
}

std::shared_ptr<ThreadSafeQueue<PendingQuestion>>
AbstractMessageBox::GetOrCreatePendingQueue(const std::string& content_type) {
    std::lock_guard lock(pending_questions_mutex_);
    auto it = pending_questions_.find(content_type);
    if (it == pending_questions_.end()) {
        auto queue = std::make_shared<ThreadSafeQueue<PendingQuestion>>();
        it = pending_questions_.emplace(content_type, std::move(queue)).first;
    }
    return it->second;
}

std::future<Answer> AbstractMessageBox::InternalAsk(const std::string& content_type,
                                                    std::optional<std::string> content,
                                                    std::optional<std::chrono::milliseconds> timeout) {
    const auto correlation_id = GenerateCorrelationId();
    QuestionMessage message{.message_box_name = name_,
                            .correlation_id = correlation_id,
                            .content_type = content_type,
                            .content = content.value_or("")};

    auto promise = std::make_shared<std::promise<Answer>>();
    auto future = promise->get_future();
    {
        std::lock_guard lock(pending_answers_mutex_);
        pending_answers_[correlation_id] = promise;
    }

    outgoing_messages_.push(message);

    if (timeout) {
        auto weak_promise = std::weak_ptr(promise);
        std::thread([this, weak_promise, correlation_id, timeout_value = *timeout]() {
            std::this_thread::sleep_for(timeout_value);
            if (auto locked = weak_promise.lock()) {
                std::lock_guard lock(pending_answers_mutex_);
                auto it = pending_answers_.find(correlation_id);
                if (it != pending_answers_.end()) {
                    try {
                        throw std::runtime_error("Request timed out");
                    } catch (...) {
                        it->second->set_exception(std::current_exception());
                    }
                    pending_answers_.erase(it);
                }
            }
        }).detach();
    }

    return future;
}

void AbstractMessageBox::DealerLoop(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        zmq::pollitem_t items[] = {{dealer_, 0, ZMQ_POLLIN, 0}};
        zmq::poll(items, 1, std::chrono::milliseconds{10});

        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t message_type_frame;
            zmq::message_t message_box_name_frame;
            zmq::message_t correlation_frame;
            zmq::message_t content_type_frame;
            zmq::message_t content_frame;

            dealer_.recv(message_type_frame, zmq::recv_flags::none);
            dealer_.recv(message_box_name_frame, zmq::recv_flags::none);
            dealer_.recv(correlation_frame, zmq::recv_flags::none);
            dealer_.recv(content_type_frame, zmq::recv_flags::none);
            dealer_.recv(content_frame, zmq::recv_flags::none);

            const std::string message_type(static_cast<char*>(message_type_frame.data()), message_type_frame.size());
            const std::string correlation_id(static_cast<char*>(correlation_frame.data()), correlation_frame.size());
            const std::string content_type(static_cast<char*>(content_type_frame.data()), content_type_frame.size());
            const std::string content(static_cast<char*>(content_frame.data()), content_frame.size());

            if (message_type == to_string(MessageType::Answer)) {
                ReceiveAnswer(AnswerMessage{.message_box_name = name_,
                                            .correlation_id = correlation_id,
                                            .content_type = content_type,
                                            .content = content});
            }
        }

        OutgoingMessage outgoing;
        while (outgoing_messages_.try_pop(outgoing)) {
            std::visit([this](auto&& message) { SendMessage(message); }, outgoing);
        }

        if (stop_token.stop_requested()) {
            break;
        }

        if (outgoing_messages_.wait_pop(outgoing, std::chrono::milliseconds{10})) {
            std::visit([this](auto&& message) { SendMessage(message); }, outgoing);
        }
    }
}

void AbstractMessageBox::SendMessage(const TellMessage& message) {
    dealer_.send(zmq::buffer(message.message_box_name), zmq::send_flags::sndmore);
    dealer_.send(zmq::buffer(std::string(to_string(MessageType::Tell))), zmq::send_flags::sndmore);
    dealer_.send(zmq::buffer(std::string{}), zmq::send_flags::sndmore);
    dealer_.send(zmq::buffer(message.content_type), zmq::send_flags::sndmore);
    dealer_.send(zmq::buffer(message.content), zmq::send_flags::none);
}

void AbstractMessageBox::SendMessage(const QuestionMessage& message) {
    dealer_.send(zmq::buffer(message.message_box_name), zmq::send_flags::sndmore);
    dealer_.send(zmq::buffer(std::string(to_string(MessageType::Question))), zmq::send_flags::sndmore);
    dealer_.send(zmq::buffer(message.correlation_id), zmq::send_flags::sndmore);
    dealer_.send(zmq::buffer(message.content_type), zmq::send_flags::sndmore);
    dealer_.send(zmq::buffer(message.content), zmq::send_flags::none);
}

void AbstractMessageBox::SendAnswer(const PendingQuestion& pending_question, const Answer& answer) {
    AnswerMessage answer_message{.message_box_name = name_,
                                 .correlation_id = pending_question.question_message.correlation_id,
                                 .content_type = answer.content_type,
                                 .content = answer.content};

    pending_question.answer_queue->push(IdentityMessage<AnswerMessage>{
        .dealer_identity = pending_question.dealer_identity,
        .message = std::move(answer_message)});
}

std::string AbstractMessageBox::GenerateCorrelationId() {
    std::uniform_int_distribution<std::uint64_t> distribution;
    std::lock_guard lock(random_mutex_);
    auto value = distribution(random_engine_);
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) {
        result[i] = digits[value & 0xF];
        value >>= 4;
    }
    return result;
}

void AbstractMessageBox::FulfillPendingAnswer(const std::string& correlation_id, const Answer& answer) {
    std::shared_ptr<std::promise<Answer>> promise;
    {
        std::lock_guard lock(pending_answers_mutex_);
        auto it = pending_answers_.find(correlation_id);
        if (it == pending_answers_.end()) {
            return;
        }
        promise = it->second;
        pending_answers_.erase(it);
    }
    promise->set_value(answer);
}

void AbstractMessageBox::FailPendingAnswer(const std::string& correlation_id, std::exception_ptr error) {
    std::shared_ptr<std::promise<Answer>> promise;
    {
        std::lock_guard lock(pending_answers_mutex_);
        auto it = pending_answers_.find(correlation_id);
        if (it == pending_answers_.end()) {
            return;
        }
        promise = it->second;
        pending_answers_.erase(it);
    }
    promise->set_exception(error);
}

} // namespace minx::zmesh
