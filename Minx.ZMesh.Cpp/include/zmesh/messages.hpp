#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

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

inline nlohmann::json serialize_message_base(const Message& message) {
    nlohmann::json json;
    json["messageType"] = std::string{to_string(message.message_type)};
    json["contentType"] = message.content_type;
    json["content"] = message.content;
    json["messageBoxName"] = message.message_box_name;
    return json;
}

inline void deserialize_message_base(const nlohmann::json& json, Message& message) {
    const auto type = json.at("messageType").get<std::string>();
    message.message_type = message_type_from_string(type);
    message.content_type = json.at("contentType").get<std::string>();
    message.content = json.at("content").get<std::string>();
    message.message_box_name = json.at("messageBoxName").get<std::string>();
}

inline std::string serialize_tell_message(const TellMessage& message) {
    auto json = serialize_message_base(message);
    return json.dump();
}

inline TellMessage deserialize_tell_message(std::string_view data) {
    auto json = nlohmann::json::parse(data);
    TellMessage message;
    deserialize_message_base(json, message);
    if (message.message_type != MessageType::Tell) {
        throw std::runtime_error("invalid serialized tell message: wrong type");
    }
    return message;
}

inline std::string serialize_question_message(const QuestionMessage& message) {
    auto json = serialize_message_base(message);
    json["correlationId"] = message.correlation_id;
    if (message.answer_content_type) {
        json["answerContentType"] = *message.answer_content_type;
    } else {
        json["answerContentType"] = nullptr;
    }
    return json.dump();
}

inline QuestionMessage deserialize_question_message(std::string_view data) {
    auto json = nlohmann::json::parse(data);
    QuestionMessage message;
    deserialize_message_base(json, message);
    if (message.message_type != MessageType::Question) {
        throw std::runtime_error("invalid serialized question message: wrong type");
    }
    message.correlation_id = json.at("correlationId").get<std::string>();
    if (json.contains("answerContentType") && !json.at("answerContentType").is_null()) {
        message.answer_content_type = json.at("answerContentType").get<std::string>();
    } else {
        message.answer_content_type = std::nullopt;
    }
    return message;
}

inline std::string serialize_answer_message(const AnswerMessage& message) {
    auto json = serialize_message_base(message);
    json["correlationId"] = message.correlation_id;
    return json.dump();
}

inline AnswerMessage deserialize_answer_message(std::string_view data) {
    auto json = nlohmann::json::parse(data);
    AnswerMessage message;
    deserialize_message_base(json, message);
    if (message.message_type != MessageType::Answer) {
        throw std::runtime_error("invalid serialized answer message: wrong type");
    }
    message.correlation_id = json.at("correlationId").get<std::string>();
    return message;
}

} // namespace zmesh

