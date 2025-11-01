#pragma once

#include <string>

namespace Minx::ZMesh
{
    struct Answer
    {
        std::string content_type;
        std::string content;

        [[nodiscard]] std::string ToString() const;
    };
}

