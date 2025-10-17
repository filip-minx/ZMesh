#pragma once

#include "Message.hpp"

#include <string>

namespace zmesh
{
    struct QuestionMessage : Message
    {
        QuestionMessage()
        {
            type = MessageType::Question;
        }

        std::string correlation_id;
        std::optional<std::string> answer_content_type;
    };
}
