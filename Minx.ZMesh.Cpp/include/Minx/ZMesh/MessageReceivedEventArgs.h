#pragma once

#include <string>

namespace Minx::ZMesh
{
    class MessageReceivedEventArgs
    {
    public:
        explicit MessageReceivedEventArgs(std::string content_type);

        [[nodiscard]] const std::string& ContentType() const noexcept;

    private:
        std::string content_type_;
    };
}

