#pragma once

#include "Answer.hpp"
#include "QuestionMessage.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace zmesh
{
    class PendingQuestion
    {
    public:
        PendingQuestion() = default;

        PendingQuestion(std::string dealer_identity,
                         QuestionMessage message,
                         std::shared_ptr<std::function<void(const Answer&)>> answer_callback)
            : dealer_identity_(std::move(dealer_identity)),
              message_(std::move(message)),
              answer_callback_(std::move(answer_callback))
        {
        }

        const std::string& dealer_identity() const noexcept
        {
            return dealer_identity_;
        }

        const QuestionMessage& message() const noexcept
        {
            return message_;
        }

        bool has_value() const noexcept
        {
            return static_cast<bool>(answer_callback_);
        }

        void answer(const Answer& answer) const
        {
            if (!answer_callback_)
            {
                throw std::logic_error("Pending question is no longer available.");
            }

            (*answer_callback_)(answer);
        }

    private:
        std::string dealer_identity_;
        QuestionMessage message_;
        std::shared_ptr<std::function<void(const Answer&)>> answer_callback_;
    };
}
