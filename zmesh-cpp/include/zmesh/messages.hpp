#pragma once

#include "message_type.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace zmesh
{
    struct Message
    {
        MessageType type{MessageType::Tell};
        std::string content_type;
        std::optional<std::string> content;
        std::string message_box_name;
    };

    struct TellMessage : Message
    {
        TellMessage()
        {
            type = MessageType::Tell;
        }
    };

    struct QuestionMessage : Message
    {
        std::string correlation_id;
        std::optional<std::string> answer_content_type;

        QuestionMessage()
        {
            type = MessageType::Question;
        }
    };

    struct AnswerMessage : Message
    {
        std::string correlation_id;

        AnswerMessage()
        {
            type = MessageType::Answer;
        }
    };

    struct IdentityAnswer
    {
        AnswerMessage message;
        std::string dealer_identity;
    };

    TellMessage tell_message_from_json(const nlohmann::json& json);
    QuestionMessage question_message_from_json(const nlohmann::json& json);
    AnswerMessage answer_message_from_json(const nlohmann::json& json);

    nlohmann::json tell_message_to_json(const TellMessage& message);
    nlohmann::json question_message_to_json(const QuestionMessage& message);
    nlohmann::json answer_message_to_json(const AnswerMessage& message);
}

