#pragma once

#include "answer.hpp"
#include "messages.hpp"
#include "pending_question.hpp"

#include <nlohmann/json.hpp>

#include <zmq.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace zmesh
{
    class MessageBox : public std::enable_shared_from_this<MessageBox>
    {
    public:
        using MessageReceivedHandler = std::function<void(const std::string&)>;

        MessageBox(std::string name,
                   std::string address,
                   std::shared_ptr<zmq::context_t> context);

        ~MessageBox();

        MessageBox(const MessageBox&) = delete;
        MessageBox& operator=(const MessageBox&) = delete;

        void tell(const std::string& content_type, const std::string& content);
        bool try_listen(const std::string& content_type, const std::function<void(const std::string&)>& handler);

        Answer ask(const std::string& content_type,
                   std::optional<std::string> content = std::nullopt,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

        std::future<Answer> async_ask(const std::string& content_type,
                                      std::optional<std::string> content = std::nullopt);

        bool try_answer(const std::string& question_content_type,
                        const std::function<Answer(const std::optional<std::string>&)>& handler);

        PendingQuestionPtr get_question(const std::string& question_content_type, bool& available);

        void on_tell_received(MessageReceivedHandler handler);
        void on_question_received(MessageReceivedHandler handler);

    private:
        friend class ZMesh;

        struct OutboundMessage
        {
            MessageType type;
            std::string payload;
        };

        struct CachedAnswer
        {
            Answer answer;
            std::chrono::steady_clock::time_point expires_at;
        };

        void write_tell_message(const TellMessage& message);
        void write_question_message(PendingQuestionPtr pending_question);
        void handle_answer_message(const AnswerMessage& message);

        void enqueue_outbound(MessageType message_type, const nlohmann::json& payload);
        void worker_loop();
        void flush_outgoing();
        void process_incoming();

        std::string generate_correlation_id() const;
        void cache_answer(const std::string& correlation_id, const Answer& answer);
        bool try_get_cached_answer(const std::string& correlation_id, Answer& answer);
        void cleanup_cache();

        std::string name_;
        std::string address_;
        std::shared_ptr<zmq::context_t> context_;
        zmq::socket_t dealer_;

        std::atomic<bool> running_{false};
        std::thread worker_;

        std::mutex outgoing_mutex_;
        std::queue<OutboundMessage> outgoing_messages_;
        std::condition_variable outgoing_cv_;

        std::mutex tell_mutex_;
        std::unordered_map<std::string, std::queue<std::string>> tell_messages_;

        std::mutex pending_questions_mutex_;
        std::unordered_map<std::string, std::deque<PendingQuestionPtr>> pending_questions_by_type_;

        std::mutex pending_questions_by_id_mutex_;
        std::unordered_map<std::string, PendingQuestionPtr> pending_questions_by_id_;

        std::mutex pending_answers_mutex_;
        std::unordered_map<std::string, std::shared_ptr<std::promise<Answer>>> pending_answers_;

        std::mutex cache_mutex_;
        std::unordered_map<std::string, CachedAnswer> response_cache_;

        std::mutex tell_handlers_mutex_;
        std::vector<MessageReceivedHandler> tell_handlers_;

        std::mutex question_handlers_mutex_;
        std::vector<MessageReceivedHandler> question_handlers_;
    };
}

