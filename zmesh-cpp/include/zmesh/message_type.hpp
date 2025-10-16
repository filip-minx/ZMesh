#pragma once

#include <string>
#include <algorithm>
#include <stdexcept>
#include <cctype>

namespace zmesh
{
    enum class MessageType
    {
        Tell,
        Question,
        Answer
    };

    inline std::string to_string(MessageType type)
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

        throw std::runtime_error("Unknown message type");
    }

    inline MessageType message_type_from_string(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (value == "tell")
        {
            return MessageType::Tell;
        }

        if (value == "question")
        {
            return MessageType::Question;
        }

        if (value == "answer")
        {
            return MessageType::Answer;
        }

        throw std::invalid_argument("Unsupported message type value");
    }
}

