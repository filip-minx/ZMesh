#pragma once

#include "Exceptions.hpp"
#include "IAbstractMessageBox.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <zmq.hpp>

namespace zmesh
{
    class ZmqMessageBox : public IAbstractMessageBox
    {
    public:
        struct Options
        {
            std::string message_box_name;
            std::string tell_publish_endpoint;
            std::string tell_subscribe_endpoint;
            std::string question_endpoint;
            std::string answer_endpoint;
        };

        explicit ZmqMessageBox(Options options);
        ~ZmqMessageBox() override;

        Subscription on_question_received(MessageReceivedHandler handler) override;
        Subscription on_tell_received(MessageReceivedHandler handler) override;

        void tell(const std::string& content_type, const std::string& content) override;
        bool try_listen(const std::string& content_type, std::function<void(const std::string&)> handler) override;
        std::future<Answer> ask(const std::string& content_type, const std::string& content) override;
        bool try_answer(const std::string& question_content_type, std::function<Answer(const std::string&)> handler) override;

        std::future<Answer> ask(const std::string& content_type) override;
        std::future<Answer> ask(const std::string& content_type, std::chrono::milliseconds timeout) override;
        std::future<Answer> ask(const std::string& content_type, const std::string& content, std::chrono::milliseconds timeout) override;
        std::future<Answer> ask(const std::string& content_type, std::stop_token cancellation_token) override;
        std::future<Answer> ask(const std::string& content_type, const std::string& content, std::stop_token cancellation_token) override;

        std::optional<PendingQuestion> get_question(const std::string& question_type, bool& available) override;

    private:
        struct QuestionContext
        {
            std::string dealer_identity;
            QuestionMessage message;
            std::shared_ptr<std::function<void(const Answer&)>> answer_callback;
        };

        struct PendingAnswer
        {
            std::string dealer_identity;
            std::string message_box_name;
            std::string correlation_id;
            Answer answer;
        };

        std::future<Answer> ask_internal(std::string content_type,
                                         std::string content,
                                         std::optional<std::chrono::milliseconds> timeout,
                                         std::optional<std::stop_token> cancellation_token);

        Answer perform_request(const std::string& content_type,
                               const std::string& content,
                               const std::optional<std::chrono::milliseconds>& timeout,
                               const std::optional<std::stop_token>& cancellation_token);

        void listen_loop(std::stop_token stop_token);
        void question_loop(std::stop_token stop_token);

        void handle_tell_message(const std::vector<std::string>& frames);
        void handle_question_message(const std::string& dealer_identity, const std::vector<std::string>& frames);

        void enqueue_answer(std::string dealer_identity,
                            std::string message_box_name,
                            std::string correlation_id,
                            Answer answer);

        void drain_pending_answers();
        void send_answer(const PendingAnswer& pending_answer);

        void remove_question_by_correlation(const std::string& correlation_id);

        Options options_;
        zmq::context_t context_;
        zmq::socket_t tell_publisher_;
        zmq::socket_t tell_subscriber_;
        zmq::socket_t answer_router_;

        Signal<MessageReceivedEventArgs> question_signal_;
        Signal<MessageReceivedEventArgs> tell_signal_;

        std::jthread tell_thread_;
        std::jthread question_thread_;

        std::unordered_map<std::string, std::function<void(const std::string&)>> tell_handlers_;
        std::unordered_map<std::string, std::function<Answer(const std::string&)>> question_handlers_;
        std::shared_mutex tell_mutex_;
        std::shared_mutex question_mutex_;

        std::deque<QuestionContext> pending_questions_;
        std::mutex pending_mutex_;

        std::queue<PendingAnswer> pending_answers_;
        std::mutex answer_mutex_;
        std::condition_variable answer_cv_;
    };
}
