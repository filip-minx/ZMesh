#include "Minx/ZMesh/PendingQuestion.h"

#include "Minx/ZMesh/AbstractMessageBox.h"

#ifdef Answer
#    undef Answer
#endif

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

        auto owner = owner_.lock();
        if (!owner)
        {
            throw std::runtime_error{"Message box no longer available to answer question."};
        }

        owner->SendAnswer(correlation_id_, answer);
    }
}

