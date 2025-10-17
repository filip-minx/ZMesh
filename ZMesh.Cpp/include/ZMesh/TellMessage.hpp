#pragma once

#include "Message.hpp"

namespace zmesh
{
    struct TellMessage : Message
    {
        TellMessage()
        {
            type = MessageType::Tell;
        }
    };
}
