#pragma once

#include <chrono>

namespace zmesh {

struct RequestOptions {
    std::chrono::milliseconds timeout{std::chrono::milliseconds{3000}};
    int max_retries{3};
};

} // namespace zmesh

