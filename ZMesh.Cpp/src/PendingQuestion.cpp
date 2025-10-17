#include "ZMesh/PendingQuestion.hpp"
#include "ZMesh/AbstractMessageBox.hpp"

namespace zmesh
{
    PendingQuestion::PendingQuestion(AbstractMessageBox& owner, std::string dealer_identity, QuestionMessage question)
        : owner_{&owner}, dealer_identity_{std::move(dealer_identity)}, question_{std::move(question)}
    {
    }

    const std::string& PendingQuestion::dealer_identity() const noexcept
    {
        return dealer_identity_;
    }

    const QuestionMessage& PendingQuestion::question_message() const noexcept
    {
        return question_;
    }

    void PendingQuestion::answer(const Answer& answer)
    {
        if (owner_)
        {
            owner_->send_answer(*this, answer);
            owner_ = nullptr;
        }
    }
}
