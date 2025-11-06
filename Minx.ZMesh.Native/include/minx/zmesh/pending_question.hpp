#pragma once

#include <memory>
#include <string>

#include "thread_safe_queue.hpp"
#include "types.hpp"

namespace minx::zmesh {

using AnswerQueue = ThreadSafeQueue<IdentityMessage<AnswerMessage>>;

struct PendingQuestion {
    std::string dealer_identity;
    QuestionMessage question_message;
    std::shared_ptr<AnswerQueue> answer_queue;
};

} // namespace minx::zmesh
