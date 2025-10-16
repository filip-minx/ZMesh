#pragma once

#include <string>
#include <utility>

namespace zmesh
{
    struct Answer
    {
        std::string content_type;
        std::string content;

        Answer() = default;

        Answer(std::string type, std::string value)
            : content_type(std::move(type)), content(std::move(value))
        {
        }

        [[nodiscard]] std::string to_string() const
        {
            return content_type + ": " + content;
        }
    };
}

