#pragma once

#include <chrono>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace minx::zmesh {

enum class MessageType {
    Tell,
    Question,
    Answer
};

inline constexpr std::string_view to_string(MessageType type) noexcept {
    switch (type) {
    case MessageType::Tell:
        return "Tell";
    case MessageType::Question:
        return "Question";
    case MessageType::Answer:
        return "Answer";
    }
    return "";
}

inline MessageType message_type_from_string(std::string_view value) {
    if (value == "Tell") {
        return MessageType::Tell;
    }
    if (value == "Question") {
        return MessageType::Question;
    }
    if (value == "Answer") {
        return MessageType::Answer;
    }
    throw std::invalid_argument("Unknown message type: " + std::string(value));
}

struct Answer {
    std::string content_type;
    std::string content;

    [[nodiscard]] std::string to_string() const {
        return content_type + ": " + content;
    }
};

inline std::ostream& operator<<(std::ostream& os, const Answer& answer) {
    return os << answer.to_string();
}

struct TellMessage {
    std::string message_box_name;
    std::string content_type;
    std::string content;
};

struct QuestionMessage {
    std::string message_box_name;
    std::string correlation_id;
    std::string content_type;
    std::string content;
};

struct AnswerMessage {
    std::string message_box_name;
    std::string correlation_id;
    std::string content_type;
    std::string content;
};

template <typename T>
struct IdentityMessage {
    std::string dealer_identity;
    T message;
};

} // namespace minx::zmesh
