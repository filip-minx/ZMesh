#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace zmesh {

/**
 * \brief Represents the type of a ZMesh message.
 */
enum class MessageType {
    Tell,
    Question,
    Answer
};

inline std::string_view to_string(MessageType type) noexcept {
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
    throw std::invalid_argument("Unknown message type: " + std::string{value});
}

struct Message {
    MessageType message_type{};
    std::string content_type;
    std::string content;
    std::string message_box_name;
};

struct QuestionMessage : Message {
    std::string correlation_id;
    std::optional<std::string> answer_content_type{};

    QuestionMessage() {
        message_type = MessageType::Question;
    }
};

struct AnswerMessage : Message {
    std::string correlation_id;

    AnswerMessage() {
        message_type = MessageType::Answer;
    }
};

struct TellMessage : Message {
    TellMessage() {
        message_type = MessageType::Tell;
    }
};

template <typename TMessage>
struct IdentityMessage {
    TMessage message;
    std::string dealer_identity;
};

namespace detail {

inline constexpr std::size_t LENGTH_SIZE = sizeof(std::uint32_t);

inline void write_string(std::string& buffer, std::string_view value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error("value too large to serialize");
    }

    const auto length = static_cast<std::uint32_t>(value.size());
    std::array<char, LENGTH_SIZE> length_bytes{};
    std::memcpy(length_bytes.data(), &length, LENGTH_SIZE);
    buffer.append(length_bytes.data(), length_bytes.size());
    buffer.append(value.begin(), value.end());
}

inline std::string read_string(std::string_view& data) {
    if (data.size() < LENGTH_SIZE) {
        throw std::runtime_error("invalid serialized message: missing string length");
    }

    std::uint32_t length{};
    std::memcpy(&length, data.data(), LENGTH_SIZE);
    data.remove_prefix(LENGTH_SIZE);

    if (data.size() < length) {
        throw std::runtime_error("invalid serialized message: truncated string");
    }

    std::string value{data.substr(0, length)};
    data.remove_prefix(length);
    return value;
}

inline void write_message_base(std::string& buffer, const Message& message) {
    write_string(buffer, message.content_type);
    write_string(buffer, message.content);
    write_string(buffer, message.message_box_name);
}

inline void read_message_base(std::string_view& data, Message& message) {
    message.content_type = read_string(data);
    message.content = read_string(data);
    message.message_box_name = read_string(data);
}

inline void write_optional_string(std::string& buffer, const std::optional<std::string>& value) {
    const char flag = value.has_value() ? 1 : 0;
    buffer.push_back(flag);
    if (value) {
        write_string(buffer, *value);
    }
}

inline std::optional<std::string> read_optional_string(std::string_view& data) {
    if (data.empty()) {
        throw std::runtime_error("invalid serialized message: missing optional flag");
    }

    const auto flag = data.front();
    data.remove_prefix(1);

    if (flag == 0) {
        return std::nullopt;
    }

    if (flag != 1) {
        throw std::runtime_error("invalid serialized message: bad optional flag");
    }

    return read_string(data);
}

} // namespace detail

inline std::string serialize_tell_message(const TellMessage& message) {
    std::string buffer;
    detail::write_message_base(buffer, message);
    return buffer;
}

inline TellMessage deserialize_tell_message(std::string_view data) {
    TellMessage message;
    detail::read_message_base(data, message);
    if (!data.empty()) {
        throw std::runtime_error("invalid serialized tell message");
    }
    return message;
}

inline std::string serialize_question_message(const QuestionMessage& message) {
    std::string buffer;
    detail::write_message_base(buffer, message);
    detail::write_string(buffer, message.correlation_id);
    detail::write_optional_string(buffer, message.answer_content_type);
    return buffer;
}

inline QuestionMessage deserialize_question_message(std::string_view data) {
    QuestionMessage message;
    detail::read_message_base(data, message);
    message.correlation_id = detail::read_string(data);
    message.answer_content_type = detail::read_optional_string(data);
    if (!data.empty()) {
        throw std::runtime_error("invalid serialized question message");
    }
    message.message_type = MessageType::Question;
    return message;
}

inline std::string serialize_answer_message(const AnswerMessage& message) {
    std::string buffer;
    detail::write_message_base(buffer, message);
    detail::write_string(buffer, message.correlation_id);
    return buffer;
}

inline AnswerMessage deserialize_answer_message(std::string_view data) {
    AnswerMessage message;
    detail::read_message_base(data, message);
    message.correlation_id = detail::read_string(data);
    if (!data.empty()) {
        throw std::runtime_error("invalid serialized answer message");
    }
    message.message_type = MessageType::Answer;
    return message;
}

} // namespace zmesh

