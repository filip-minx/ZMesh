#pragma once

#include "Message.hpp"

#include <string>

namespace zmesh
{
    struct AnswerMessage : Message
    {
        AnswerMessage()
        {
            type = MessageType::Answer;
        }

        std::string correlation_id;
    };
}
