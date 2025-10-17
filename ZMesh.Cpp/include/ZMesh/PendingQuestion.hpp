#pragma once

#include "IPendingQuestion.hpp"

#include <memory>

namespace zmesh
{
    class AbstractMessageBox;

    class PendingQuestion final : public IPendingQuestion, public std::enable_shared_from_this<PendingQuestion>
    {
    public:
        PendingQuestion(AbstractMessageBox& owner, std::string dealer_identity, QuestionMessage question);

        [[nodiscard]] const std::string& dealer_identity() const noexcept override;
        [[nodiscard]] const QuestionMessage& question_message() const noexcept override;

        void answer(const Answer& answer) override;

    private:
        AbstractMessageBox* owner_;
        std::string dealer_identity_;
        QuestionMessage question_;
    };
}
