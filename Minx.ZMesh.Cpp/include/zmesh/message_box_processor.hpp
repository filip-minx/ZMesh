#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <typeinfo>
#include <unordered_map>

#include "message_box.hpp"
#include "typed_message_box.hpp"

namespace zmesh {

struct MessageProcessingOptions {
    std::function<void(MessageType, const std::string&)> on_missing_handler{};
    std::function<void(const std::exception&)> on_unhandled_exception{};
};

class MessageBoxProcessor {
public:
    explicit MessageBoxProcessor(std::shared_ptr<TypedMessageBox> message_box,
                                 MessageProcessingOptions options = {});
    ~MessageBoxProcessor();

    MessageBoxProcessor(const MessageBoxProcessor&) = delete;
    MessageBoxProcessor& operator=(const MessageBoxProcessor&) = delete;

    template <typename Message>
    void listen(const std::function<void(const Message&)>& handler) {
        const auto content_type = std::string{typeid(Message).name()};
        tell_handlers_[content_type] = [handler](TypedMessageBox& box) {
            box.template try_listen<Message>(handler);
        };
    }

    template <typename Question, typename Answer>
    void answer(const std::function<Answer(const Question&)>& handler) {
        const auto content_type = std::string{typeid(Question).name()};
        question_handlers_[content_type] = [handler](TypedMessageBox& box) {
            box.template try_answer<Question, Answer>(handler);
        };
    }

    void process_one();
    void process_all();

private:
    void enqueue(MessageType type, std::string content_type);
    std::optional<std::pair<MessageType, std::string>> try_dequeue();
    std::optional<std::pair<MessageType, std::string>> wait_dequeue();
    void handle_message(MessageType type, const std::string& content_type);
    void handle_tell(const std::string& content_type);
    void handle_question(const std::string& content_type);
    void invoke_missing_handler(MessageType type, const std::string& content_type);
    void handle_exception(const std::exception& ex);

    std::shared_ptr<TypedMessageBox> message_box_;
    MessageProcessingOptions options_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::pair<MessageType, std::string>> message_queue_;

    std::unordered_map<std::string, std::function<void(TypedMessageBox&)>> tell_handlers_;
    std::unordered_map<std::string, std::function<void(TypedMessageBox&)>> question_handlers_;

    std::atomic_bool disposed_{false};
    size_t tell_subscription_token_{0};
    size_t question_subscription_token_{0};
};

} // namespace zmesh

