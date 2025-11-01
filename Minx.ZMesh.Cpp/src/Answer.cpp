#include "Minx/ZMesh/Answer.h"

namespace Minx::ZMesh
{
    std::string Answer::ToString() const
    {
        return content_type + ": " + content;
    }
}

