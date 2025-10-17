#include "zmesh/message_box_processor.hpp"

#include <stdexcept>

namespace zmesh {

MessageBoxProcessor::MessageBoxProcessor(std::shared_ptr<TypedMessageBox> message_box,
                                         MessageProcessingOptions options)
    : message_box_{std::move(message_box)}, options_{std::move(options)} {
    tell_subscription_token_ = message_box_->add_tell_received_handler(
        [this](const MessageReceivedEventArgs& args) { enqueue(MessageType::Tell, args.content_type); });

    question_subscription_token_ = message_box_->add_question_received_handler(
        [this](const MessageReceivedEventArgs& args) { enqueue(MessageType::Question, args.content_type); });
}

MessageBoxProcessor::~MessageBoxProcessor() {
    disposed_.store(true);
    queue_cv_.notify_all();

    if (message_box_) {
        message_box_->remove_tell_received_handler(tell_subscription_token_);
        message_box_->remove_question_received_handler(question_subscription_token_);
    }
}

void MessageBoxProcessor::process_one() {
    if (auto item = try_dequeue()) {
        handle_message(item->first, item->second);
    }
}

void MessageBoxProcessor::process_all() {
    while (!disposed_.load()) {
        auto item = wait_dequeue();
        if (!item.has_value()) {
            break;
        }

        handle_message(item->first, item->second);
    }
}

void MessageBoxProcessor::enqueue(MessageType type, std::string content_type) {
    {
        std::lock_guard lock(queue_mutex_);
        message_queue_.emplace(type, std::move(content_type));
    }
    queue_cv_.notify_one();
}

std::optional<std::pair<MessageType, std::string>> MessageBoxProcessor::try_dequeue() {
    std::lock_guard lock(queue_mutex_);
    if (message_queue_.empty()) {
        return std::nullopt;
    }

    auto item = std::move(message_queue_.front());
    message_queue_.pop();
    return item;
}

std::optional<std::pair<MessageType, std::string>> MessageBoxProcessor::wait_dequeue() {
    std::unique_lock lock(queue_mutex_);
    queue_cv_.wait(lock, [&] { return disposed_.load() || !message_queue_.empty(); });

    if (disposed_.load() && message_queue_.empty()) {
        return std::nullopt;
    }

    auto item = std::move(message_queue_.front());
    message_queue_.pop();
    return item;
}

void MessageBoxProcessor::handle_message(MessageType type, const std::string& content_type) {
    switch (type) {
    case MessageType::Tell:
        handle_tell(content_type);
        break;
    case MessageType::Question:
        handle_question(content_type);
        break;
    default:
        throw std::invalid_argument("Unexpected message type");
    }
}

void MessageBoxProcessor::handle_tell(const std::string& content_type) {
    auto it = tell_handlers_.find(content_type);
    if (it == tell_handlers_.end()) {
        invoke_missing_handler(MessageType::Tell, content_type);
        return;
    }

    try {
        it->second(*message_box_);
    } catch (const std::exception& ex) {
        handle_exception(ex);
    }
}

void MessageBoxProcessor::handle_question(const std::string& content_type) {
    auto it = question_handlers_.find(content_type);
    if (it == question_handlers_.end()) {
        invoke_missing_handler(MessageType::Question, content_type);
        return;
    }

    try {
        it->second(*message_box_);
    } catch (const std::exception& ex) {
        handle_exception(ex);
    }
}

void MessageBoxProcessor::invoke_missing_handler(MessageType type, const std::string& content_type) {
    if (options_.on_missing_handler) {
        options_.on_missing_handler(type, content_type);
    } else {
        throw std::runtime_error("No handler registered for message type: " + content_type);
    }
}

void MessageBoxProcessor::handle_exception(const std::exception& ex) {
    if (options_.on_unhandled_exception) {
        options_.on_unhandled_exception(ex);
    } else {
        throw;
    }
}

} // namespace zmesh

