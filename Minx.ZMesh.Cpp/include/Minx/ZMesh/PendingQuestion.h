#pragma once

#include "Response.h"

#include <memory>
#include <string>
#include <utility>

#ifdef Answer
#    undef Answer
#endif

namespace Minx::ZMesh
{
    class AbstractMessageBox;

    class PendingQuestion
    {
    public:
        PendingQuestion() = default;
        PendingQuestion(std::weak_ptr<AbstractMessageBox> owner,
                        std::string correlation_id,
                        std::string content_type,
                        std::string content);

        [[nodiscard]] bool Valid() const noexcept;
        [[nodiscard]] const std::string& CorrelationId() const noexcept;
        [[nodiscard]] const std::string& ContentType() const noexcept;
        [[nodiscard]] const std::string& Content() const noexcept;

        void Answer(const Response& response) const;

    private:
        std::weak_ptr<AbstractMessageBox> owner_;
        std::string correlation_id_;
        std::string content_type_;
        std::string content_;
    };
}

