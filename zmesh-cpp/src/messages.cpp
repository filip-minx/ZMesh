#include "zmesh/messages.hpp"

#include <nlohmann/json.hpp>

#include <chrono>

namespace zmesh
{
    namespace
    {
        std::optional<std::string> read_optional_string(const nlohmann::json& json, const char* key)
        {
            auto it = json.find(key);
            if (it == json.end() || it->is_null())
            {
                return std::nullopt;
            }

            return it->get<std::string>();
        }
    }

    TellMessage tell_message_from_json(const nlohmann::json& json)
    {
        TellMessage message;
        message.type = MessageType::Tell;
        message.content_type = json.value("ContentType", std::string{});
        message.content = read_optional_string(json, "Content");
        message.message_box_name = json.value("MessageBoxName", std::string{});
        return message;
    }

    QuestionMessage question_message_from_json(const nlohmann::json& json)
    {
        QuestionMessage message;
        message.type = MessageType::Question;
        message.content_type = json.value("ContentType", std::string{});
        message.content = read_optional_string(json, "Content");
        message.message_box_name = json.value("MessageBoxName", std::string{});
        message.correlation_id = json.value("CorrelationId", std::string{});
        message.answer_content_type = read_optional_string(json, "AnswerContentType");
        return message;
    }

    AnswerMessage answer_message_from_json(const nlohmann::json& json)
    {
        AnswerMessage message;
        message.type = MessageType::Answer;
        message.content_type = json.value("ContentType", std::string{});
        message.content = read_optional_string(json, "Content");
        message.message_box_name = json.value("MessageBoxName", std::string{});
        message.correlation_id = json.value("CorrelationId", std::string{});
        return message;
    }

    nlohmann::json tell_message_to_json(const TellMessage& message)
    {
        nlohmann::json json;
        json["MessageType"] = to_string(MessageType::Tell);
        json["ContentType"] = message.content_type;
        json["MessageBoxName"] = message.message_box_name;
        if (message.content)
        {
            json["Content"] = *message.content;
        }
        else
        {
            json["Content"] = nullptr;
        }
        return json;
    }

    nlohmann::json question_message_to_json(const QuestionMessage& message)
    {
        nlohmann::json json;
        json["MessageType"] = to_string(MessageType::Question);
        json["ContentType"] = message.content_type;
        json["MessageBoxName"] = message.message_box_name;
        json["CorrelationId"] = message.correlation_id;
        if (message.content)
        {
            json["Content"] = *message.content;
        }
        else
        {
            json["Content"] = nullptr;
        }
        if (message.answer_content_type)
        {
            json["AnswerContentType"] = *message.answer_content_type;
        }
        else
        {
            json["AnswerContentType"] = nullptr;
        }
        return json;
    }

    nlohmann::json answer_message_to_json(const AnswerMessage& message)
    {
        nlohmann::json json;
        json["MessageType"] = to_string(MessageType::Answer);
        json["ContentType"] = message.content_type;
        json["MessageBoxName"] = message.message_box_name;
        json["CorrelationId"] = message.correlation_id;
        if (message.content)
        {
            json["Content"] = *message.content;
        }
        else
        {
            json["Content"] = nullptr;
        }
        return json;
    }
}

