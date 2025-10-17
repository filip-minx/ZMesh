#pragma once

#include <string_view>

namespace zmesh
{
    enum class MessageType
    {
        Tell,
        Question,
        Answer
    };

    [[nodiscard]] constexpr std::string_view to_string(MessageType type) noexcept
    {
        switch (type)
        {
        case MessageType::Tell:
            return "Tell";
        case MessageType::Question:
            return "Question";
        case MessageType::Answer:
            return "Answer";
        }
        return "Tell";
    }
}
