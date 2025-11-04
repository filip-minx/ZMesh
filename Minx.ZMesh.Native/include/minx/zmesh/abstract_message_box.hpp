#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include <zmq.hpp>

#include "iabstract_message_box.hpp"
#include "thread_safe_queue.hpp"

namespace minx::zmesh {

class ZMesh;

class AbstractMessageBox : public IAbstractMessageBox, public std::enable_shared_from_this<AbstractMessageBox> {
public:
    AbstractMessageBox(std::string name,
                       std::string address,
                       zmq::context_t& context,
                       std::shared_ptr<AnswerQueue> answer_queue);
    ~AbstractMessageBox() override;

    void Tell(std::string content_type, std::string content) override;
    bool TryListen(const std::string& content_type, const TellHandler& handler) override;

    std::future<Answer> Ask(const std::string& content_type) override;
    std::future<Answer> Ask(const std::string& content_type, std::string content) override;
    std::future<Answer> Ask(const std::string& content_type, std::chrono::milliseconds timeout) override;
    std::future<Answer> Ask(const std::string& content_type, std::string content, std::chrono::milliseconds timeout) override;

    bool TryAnswer(const std::string& question_content_type, const QuestionHandler& handler) override;
    std::optional<PendingQuestion> GetQuestion(const std::string& question_type) override;

    void ReceiveTell(const TellMessage& message);
    void ReceiveQuestion(const PendingQuestion& pending_question);
    void ReceiveAnswer(const AnswerMessage& message);

private:
    using OutgoingMessage = std::variant<TellMessage, QuestionMessage>;

    std::shared_ptr<ThreadSafeQueue<std::string>> GetOrCreateMessageQueue(const std::string& content_type);
    std::shared_ptr<ThreadSafeQueue<PendingQuestion>> GetOrCreatePendingQueue(const std::string& content_type);

    std::future<Answer> InternalAsk(const std::string& content_type,
                                    std::optional<std::string> content,
                                    std::optional<std::chrono::milliseconds> timeout);

    void DealerLoop(std::stop_token stop_token);
    void SendMessage(const TellMessage& message);
    void SendMessage(const QuestionMessage& message);
    void SendAnswer(const PendingQuestion& pending_question, const Answer& answer);

    std::string GenerateCorrelationId();

    void FulfillPendingAnswer(const std::string& correlation_id, const Answer& answer);
    void FailPendingAnswer(const std::string& correlation_id, std::exception_ptr error);

    std::string name_;
    std::string address_;
    zmq::context_t& context_;
    std::shared_ptr<AnswerQueue> answer_queue_;

    ThreadSafeQueue<OutgoingMessage> outgoing_messages_;
    zmq::socket_t dealer_;
    std::jthread dealer_thread_;

    std::mutex messages_mutex_;
    std::unordered_map<std::string, std::shared_ptr<ThreadSafeQueue<std::string>>> messages_;

    std::mutex pending_questions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<ThreadSafeQueue<PendingQuestion>>> pending_questions_;

    std::mutex pending_answers_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::promise<Answer>>> pending_answers_;

    std::mutex random_mutex_;
    std::mt19937_64 random_engine_;
};

} // namespace minx::zmesh
