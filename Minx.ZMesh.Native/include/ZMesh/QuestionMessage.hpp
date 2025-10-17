#pragma once

#include <string>

namespace zmesh
{
    struct QuestionMessage
    {
        std::string message_box_name;
        std::string content_type;
        std::string content;
        std::string correlation_id;
        std::string answer_content_type;
    };
}
