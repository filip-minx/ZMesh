#pragma once

#include <string>

namespace zmesh
{
    class MessageReceivedEventArgs
    {
    public:
        explicit MessageReceivedEventArgs(std::string content_type)
            : content_type_(std::move(content_type))
        {
        }

        [[nodiscard]] const std::string& content_type() const noexcept
        {
            return content_type_;
        }

    private:
        std::string content_type_;
    };
}
