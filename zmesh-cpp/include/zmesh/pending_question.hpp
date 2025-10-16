#pragma once

#include "answer.hpp"
#include "messages.hpp"

#include <functional>
#include <memory>
#include <utility>

namespace zmesh
{
    class PendingQuestion
    {
    public:
        using AnswerSender = std::function<void(IdentityAnswer&&)>;

        PendingQuestion(std::string message_box_name, AnswerSender sender)
            : message_box_name_(std::move(message_box_name)), send_answer_(std::move(sender))
        {
        }

        QuestionMessage question_message;
        std::string dealer_identity;

        void answer(const Answer& answer) const
        {
            if (!send_answer_)
            {
                return;
            }

            AnswerMessage answer_message;
            answer_message.content_type = answer.content_type;
            answer_message.content = answer.content;
            answer_message.message_box_name = message_box_name_;
            answer_message.correlation_id = question_message.correlation_id;

            IdentityAnswer wrapped_answer{std::move(answer_message), dealer_identity};
            send_answer_(std::move(wrapped_answer));
        }

    private:
        std::string message_box_name_;
        AnswerSender send_answer_;
    };

    using PendingQuestionPtr = std::shared_ptr<PendingQuestion>;
}

