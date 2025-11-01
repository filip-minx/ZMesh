#include "Minx/ZMesh/MessageReceivedEventArgs.h"

#include <utility>

namespace Minx::ZMesh
{
    MessageReceivedEventArgs::MessageReceivedEventArgs(std::string content_type)
        : content_type_{std::move(content_type)}
    {
    }

    const std::string& MessageReceivedEventArgs::ContentType() const noexcept
    {
        return content_type_;
    }
}

