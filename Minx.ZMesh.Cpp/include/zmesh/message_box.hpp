#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <zmq.hpp>

#include "messages.hpp"
#include "request_options.hpp"

namespace zmesh {

class MessageBox;
class ZMesh;

struct Answer {
    std::string content_type;
    std::string content;
};

struct MessageReceivedEventArgs {
    std::string content_type;
};

class PendingQuestion {
public:
    PendingQuestion(MessageBox& owner, QuestionMessage question, std::string dealer_identity);

    [[nodiscard]] const QuestionMessage& question() const noexcept;
    [[nodiscard]] const std::string& dealer_identity() const noexcept;

    void answer(const Answer& answer);

private:
    MessageBox* owner_;
    QuestionMessage question_;
    std::string dealer_identity_;
};

class MessageBox {
public:
    using TellHandler = std::function<void(const MessageReceivedEventArgs&)>;
    using QuestionHandler = std::function<void(const MessageReceivedEventArgs&)>;

    MessageBox(std::string name,
               std::string endpoint,
               zmq::context_t& context,
               std::function<void(const std::string&, AnswerMessage)> answer_sender);
    ~MessageBox();

    MessageBox(const MessageBox&) = delete;
    MessageBox& operator=(const MessageBox&) = delete;
    MessageBox(MessageBox&&) = delete;
    MessageBox& operator=(MessageBox&&) = delete;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& endpoint() const noexcept { return endpoint_; }
    [[nodiscard]] const std::string& identity() const noexcept { return identity_; }

    Answer ask(const std::string& content_type,
               std::optional<std::string> content = std::nullopt,
               RequestOptions options = {});
    Answer ask(const std::string& content_type, RequestOptions options);
    Answer ask(const std::string& content_type, std::chrono::milliseconds timeout);
    Answer ask(const std::string& content_type, const std::string& content, std::chrono::milliseconds timeout);

    void tell(const std::string& content_type, const std::string& content);

    bool try_listen(const std::string& content_type, const std::function<void(const std::string&)>& handler);
    bool try_answer(const std::string& question_content_type, const std::function<Answer(const std::string&)>& handler);

    std::shared_ptr<PendingQuestion> get_question(const std::string& question_type, bool& available);

    size_t add_tell_received_handler(TellHandler handler);
    void remove_tell_received_handler(size_t token);
    size_t add_question_received_handler(QuestionHandler handler);
    void remove_question_received_handler(size_t token);

private:
    friend class ZMesh;
    friend class PendingQuestion;

    struct OutgoingMessage {
        MessageType type;
        std::string payload;
    };

    struct CachedAnswer {
        Answer answer;
        std::chrono::steady_clock::time_point expires_at;
    };

    void worker_loop(std::stop_token stop_token);
    void send_message(zmq::socket_t& socket, const OutgoingMessage& message);
    void handle_answer(const AnswerMessage& answer);
    void enqueue_outgoing(MessageType type, std::string payload);
    void prune_expired_cache();
    std::optional<Answer> get_cached_answer(const std::string& correlation_id);
    void cache_answer(const std::string& correlation_id, const Answer& answer);
    void send_answer(const PendingQuestion& pending_question, const Answer& answer);
    void write_tell_message(const TellMessage& message);
    void write_question_message(const QuestionMessage& message, const std::string& dealer_identity);
    void notify_tell_received(const std::string& content_type);
    void notify_question_received(const std::string& content_type);

    std::string name_;
    std::string endpoint_;
    zmq::context_t& context_;
    std::string identity_;

    std::jthread worker_;

    std::mutex outgoing_mutex_;
    std::condition_variable outgoing_cv_;
    std::deque<OutgoingMessage> outgoing_messages_;

    std::mutex pending_answers_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::promise<Answer>>> pending_answers_;

    std::mutex tells_mutex_;
    std::unordered_map<std::string, std::queue<std::string>> tell_messages_;

    std::mutex pending_questions_mutex_;
    std::unordered_map<std::string, std::queue<std::shared_ptr<PendingQuestion>>> pending_questions_;

    std::mutex seen_questions_mutex_;
    std::unordered_set<std::string> seen_question_ids_;

    std::mutex cache_mutex_;
    std::unordered_map<std::string, CachedAnswer> response_cache_;

    std::mutex tell_handlers_mutex_;
    std::unordered_map<size_t, TellHandler> tell_handlers_;

    std::mutex question_handlers_mutex_;
    std::unordered_map<size_t, QuestionHandler> question_handlers_;

    std::atomic_size_t next_handler_token_{0};

    std::function<void(const std::string&, AnswerMessage)> answer_sender_;
};

} // namespace zmesh

