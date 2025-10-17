#pragma once

#include "IAbstractMessageBox.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

namespace zmesh
{
    class AbstractMessageBox : public IAbstractMessageBox
    {
        friend class PendingQuestion;

    public:
        AbstractMessageBox(std::string name, std::string address);
        ~AbstractMessageBox() override;

        AbstractMessageBox(const AbstractMessageBox&) = delete;
        AbstractMessageBox& operator=(const AbstractMessageBox&) = delete;
        AbstractMessageBox(AbstractMessageBox&&) = delete;
        AbstractMessageBox& operator=(AbstractMessageBox&&) = delete;

        [[nodiscard]] EventConnection on_question_received(EventHandler handler) override;
        [[nodiscard]] EventConnection on_tell_received(EventHandler handler) override;

        void tell(const std::string& content_type, const std::string& content) override;
        bool try_listen(const std::string& content_type, ContentHandler handler) override;
        std::future<Answer> ask(const std::string& content_type) override;
        std::future<Answer> ask(const std::string& content_type, const std::string& content) override;
        std::future<Answer> ask(const std::string& content_type, std::stop_token stop_token) override;
        std::future<Answer> ask(const std::string& content_type, std::chrono::milliseconds timeout) override;
        std::future<Answer> ask(const std::string& content_type, const std::string& content, std::stop_token stop_token) override;
        std::future<Answer> ask(const std::string& content_type, const std::string& content, std::chrono::milliseconds timeout) override;
        bool try_answer(const std::string& question_content_type, AnswerHandler handler) override;
        PendingQuestionPtr get_question(const std::string& question_type, bool& available) override;

    private:
        struct PendingAnswerState
        {
            std::shared_ptr<std::promise<Answer>> promise;
        };

        struct InternalAskResult
        {
            std::future<Answer> future;
            std::string correlation_id;
        };

        using QuestionQueue = std::queue<std::shared_ptr<PendingQuestion>>;
        using MessageQueue = std::queue<std::string>;

        void receive_loop();
        void handle_tell_message(const nlohmann::json& payload);
        void handle_question_message(const nlohmann::json& payload, std::string dealer_identity);
        void handle_answer_message(const nlohmann::json& payload);
        void dispatch_event(std::unordered_map<std::size_t, EventHandler>& handlers, std::mutex& mutex, const MessageReceivedEventArgs& args);

        InternalAskResult internal_ask(std::string content_type, std::optional<std::string> content);
        std::future<Answer> ask_with_timeout(std::string content_type, std::optional<std::string> content, std::chrono::milliseconds timeout);

        void cancel_pending_answer(const std::string& correlation_id, std::exception_ptr ex) noexcept;
        void send_message(MessageType type, const nlohmann::json& payload);
        void send_answer(const PendingQuestion& question, const Answer& answer);

        std::string generate_correlation_id() const;

        std::string name_;
        zmq::context_t context_;
        zmq::socket_t dealer_socket_;

        std::atomic<bool> running_{true};
        std::thread receive_thread_;

        mutable std::mutex socket_mutex_;

        std::mutex message_mutex_;
        std::unordered_map<std::string, MessageQueue> messages_;

        std::mutex pending_question_mutex_;
        std::unordered_map<std::string, QuestionQueue> pending_questions_;
        std::unordered_map<std::string, std::shared_ptr<PendingQuestion>> pending_questions_by_id_;

        std::mutex pending_answer_mutex_;
        std::unordered_map<std::string, PendingAnswerState> pending_answers_;

        std::mutex question_event_mutex_;
        std::unordered_map<std::size_t, EventHandler> question_handlers_;

        std::mutex tell_event_mutex_;
        std::unordered_map<std::size_t, EventHandler> tell_handlers_;

        std::atomic<std::size_t> next_event_id_{1};
        std::shared_ptr<bool> alive_flag_{std::make_shared<bool>(true)};
    };
}
