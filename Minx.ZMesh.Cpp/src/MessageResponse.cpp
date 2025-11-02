#include "Minx/ZMesh/MessageResponse.h"

namespace Minx::ZMesh
{
    std::string MessageResponse::ToString() const
    {
        return content_type + ": " + content;
    }
}

