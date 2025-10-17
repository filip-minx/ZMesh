#pragma once

#include <optional>
#include <stdexcept>
#include <string>

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

inline void to_json(nlohmann::json& j, const Message& message) {
    j = nlohmann::json{
        {"ContentType", message.content_type},
        {"Content", message.content},
        {"MessageBoxName", message.message_box_name}
    };
}

inline void from_json(const nlohmann::json& j, Message& message) {
    j.at("ContentType").get_to(message.content_type);
    j.at("Content").get_to(message.content);
    j.at("MessageBoxName").get_to(message.message_box_name);
}

inline void to_json(nlohmann::json& j, const QuestionMessage& message) {
    to_json(j, static_cast<const Message&>(message));
    j["CorrelationId"] = message.correlation_id;
    if (message.answer_content_type.has_value()) {
        j["AnswerContentType"] = message.answer_content_type.value();
    }
}

inline void from_json(const nlohmann::json& j, QuestionMessage& message) {
    from_json(j, static_cast<Message&>(message));
    j.at("CorrelationId").get_to(message.correlation_id);
    if (auto it = j.find("AnswerContentType"); it != j.end()) {
        message.answer_content_type = it->get<std::string>();
    }
    message.message_type = MessageType::Question;
}

inline void to_json(nlohmann::json& j, const AnswerMessage& message) {
    to_json(j, static_cast<const Message&>(message));
    j["CorrelationId"] = message.correlation_id;
}

inline void from_json(const nlohmann::json& j, AnswerMessage& message) {
    from_json(j, static_cast<Message&>(message));
    j.at("CorrelationId").get_to(message.correlation_id);
    message.message_type = MessageType::Answer;
}

inline void to_json(nlohmann::json& j, const TellMessage& message) {
    to_json(j, static_cast<const Message&>(message));
}

inline void from_json(const nlohmann::json& j, TellMessage& message) {
    from_json(j, static_cast<Message&>(message));
    message.message_type = MessageType::Tell;
}

} // namespace zmesh

