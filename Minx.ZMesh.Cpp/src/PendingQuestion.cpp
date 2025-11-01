#include "Minx/ZMesh/PendingQuestion.h"

#include "Minx/ZMesh/AbstractMessageBox.h"

#include <stdexcept>

namespace Minx::ZMesh
{
    PendingQuestion::PendingQuestion(std::weak_ptr<AbstractMessageBox> owner,
                                     std::string correlation_id,
                                     std::string content_type,
                                     std::string content)
        : owner_{std::move(owner)}
        , correlation_id_{std::move(correlation_id)}
        , content_type_{std::move(content_type)}
        , content_{std::move(content)}
    {
    }

    bool PendingQuestion::Valid() const noexcept
    {
        return !correlation_id_.empty();
    }

    const std::string& PendingQuestion::CorrelationId() const noexcept
    {
        return correlation_id_;
    }

    const std::string& PendingQuestion::ContentType() const noexcept
    {
        return content_type_;
    }

    const std::string& PendingQuestion::Content() const noexcept
    {
        return content_;
    }

    void PendingQuestion::Answer(const Answer& answer) const
    {
        if (!Valid())
        {
            throw std::logic_error{"PendingQuestion is not valid."};
        }

        if (auto owner = owner_.lock(); owner)
        {
            owner->SendAnswer(correlation_id_, answer);
        }
        else
        {
            throw std::runtime_error{"Message box no longer available to answer question."};
        }
    }
}

