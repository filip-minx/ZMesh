#pragma once

#include <stdexcept>
#include <string>

namespace zmesh
{
    class TimeoutError : public std::runtime_error
    {
    public:
        explicit TimeoutError(const std::string& message)
            : std::runtime_error(message)
        {
        }
    };

    class OperationCancelled : public std::runtime_error
    {
    public:
        explicit OperationCancelled(const std::string& message)
            : std::runtime_error(message)
        {
        }
    };
}
