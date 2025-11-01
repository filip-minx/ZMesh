#pragma once

#include "IAbstractMessageBox.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zmq.hpp>

namespace Minx::ZMesh
{
    class AbstractMessageBox : public IAbstractMessageBox, public std::enable_shared_from_this<AbstractMessageBox>
    {
    public:
        enum class ConnectionMode
        {
            Bind,
            Connect
        };

        AbstractMessageBox(std::string endpoint, ConnectionMode mode = ConnectionMode::Connect);
        ~AbstractMessageBox() override;

        AbstractMessageBox(const AbstractMessageBox&) = delete;
        AbstractMessageBox& operator=(const AbstractMessageBox&) = delete;
        AbstractMessageBox(AbstractMessageBox&&) = delete;
        AbstractMessageBox& operator=(AbstractMessageBox&&) = delete;

        void OnQuestionReceived(QuestionReceivedHandler handler) override;
        void OnTellReceived(TellReceivedHandler handler) override;

        void Tell(std::string_view content_type, std::string_view content) override;
        bool TryListen(std::string content_type, ListenHandler handler) override;
        Answer Ask(std::string_view content_type) override;
        Answer Ask(std::string_view content_type, std::string_view content) override;
        Answer Ask(std::string_view content_type, std::string_view content, std::chrono::milliseconds timeout) override;
        Answer Ask(std::string_view content_type, std::string_view content, std::stop_token stop_token) override;
        bool TryAnswer(std::string question_content_type, AnswerHandler handler) override;

        std::optional<PendingQuestion> GetQuestion(std::string question_type) override;

    private:
        friend class PendingQuestion;

        enum class MessageType
        {
            Tell,
            Question,
            Answer
        };

        void RunReceiver(std::stop_token stop_token);
        void DispatchTell(const std::string& content_type, const std::string& content);
        void DispatchQuestion(const std::string& correlation_id,
                              const std::string& content_type,
                              const std::string& content);
        void DispatchAnswer(const std::string& correlation_id,
                            const std::string& content_type,
                            const std::string& content);

        void SendMessage(MessageType type,
                         std::string_view content_type,
                         std::string_view correlation_id,
                         std::string_view content);
        void SendAnswer(const std::string& correlation_id, const Answer& answer) const;

        [[nodiscard]] std::string NextCorrelationId();

        std::string endpoint_;
        ConnectionMode mode_;
        zmq::context_t context_;
        zmq::socket_t socket_;
        std::jthread receiver_thread_;

        mutable std::mutex event_mutex_;
        std::vector<QuestionReceivedHandler> question_handlers_;
        std::vector<TellReceivedHandler> tell_handlers_;

        std::mutex listen_mutex_;
        std::unordered_map<std::string, ListenHandler> listen_handlers_;
        std::unordered_map<std::string, AnswerHandler> answer_handlers_;

        std::mutex pending_answers_mutex_;
        std::unordered_map<std::string, std::promise<Answer>> pending_answers_;

        std::mutex pending_questions_mutex_;
        std::unordered_map<std::string, std::queue<PendingQuestion>> pending_questions_;

        std::mutex correlation_mutex_;
        std::mt19937_64 random_engine_;
    };
}

