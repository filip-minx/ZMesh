#include "Minx/ZMesh/Response.h"

namespace Minx::ZMesh
{
    std::string Response::ToString() const
    {
        return content_type + ": " + content;
    }
}

