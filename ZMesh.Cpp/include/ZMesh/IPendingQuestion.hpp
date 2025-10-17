#pragma once

#include "QuestionMessage.hpp"
#include "Answer.hpp"

#include <memory>
#include <string>

namespace zmesh
{
    class IPendingQuestion
    {
    public:
        virtual ~IPendingQuestion() = default;

        [[nodiscard]] virtual const std::string& dealer_identity() const noexcept = 0;
        [[nodiscard]] virtual const QuestionMessage& question_message() const noexcept = 0;

        virtual void answer(const Answer& answer) = 0;
    };

    using PendingQuestionPtr = std::shared_ptr<IPendingQuestion>;
}
