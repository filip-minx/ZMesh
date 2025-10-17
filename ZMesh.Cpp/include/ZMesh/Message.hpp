#pragma once

#include "MessageType.hpp"

#include <string>
#include <optional>

namespace zmesh
{
    struct Message
    {
        MessageType type{};
        std::string content_type;
        std::optional<std::string> content;
        std::string message_box_name;
    };
}
