#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>

#include <nlohmann/json.hpp>

#include "message_box.hpp"

namespace zmesh {

class TypedMessageBox {
public:
    explicit TypedMessageBox(std::shared_ptr<MessageBox> inner) : inner_{std::move(inner)} {}

    [[nodiscard]] const std::shared_ptr<MessageBox>& inner() const noexcept { return inner_; }

    Answer ask_raw(const std::string& content_type,
                   std::optional<std::string> content = std::nullopt,
                   RequestOptions options = {}) {
        return inner_->ask(content_type, std::move(content), options);
    }

    template <typename Question, typename AnswerType>
    AnswerType ask(RequestOptions options = {}) {
        return ask<Question, AnswerType>(Question{}, options);
    }

    template <typename Question, typename AnswerType>
    AnswerType ask(const Question& question, RequestOptions options = {}) {
        auto question_json = serialize(question);
        auto answer = inner_->ask(typeid(Question).name(), question_json.dump(), options);
        auto json = nlohmann::json::parse(answer.content);
        return json.template get<AnswerType>();
    }

    void tell(const std::string& content_type, const std::string& content) { inner_->tell(content_type, content); }

    template <typename Message>
    void tell(const Message& message) {
        auto message_json = serialize(message);
        inner_->tell(typeid(Message).name(), message_json.dump());
    }

    bool try_answer(const std::string& question_content_type,
                    const std::function<Answer(const std::string&)>& handler) {
        return inner_->try_answer(question_content_type, handler);
    }

    template <typename Question, typename AnswerType>
    bool try_answer(const std::function<AnswerType(const Question&)>& handler) {
        return inner_->try_answer(
            typeid(Question).name(),
            [handler](const std::string& content) {
                auto json = nlohmann::json::parse(content);
                auto question = json.template get<Question>();
                auto answer_json = serialize(handler(question));
                return Answer{
                    .content_type = typeid(AnswerType).name(),
                    .content = answer_json.dump()};
            });
    }

    bool try_listen(const std::string& content_type, const std::function<void(const std::string&)>& handler) {
        return inner_->try_listen(content_type, handler);
    }

    template <typename Message>
    bool try_listen(const std::function<void(const Message&)>& handler) {
        return inner_->try_listen(
            typeid(Message).name(),
            [handler](const std::string& content) {
                auto json = nlohmann::json::parse(content);
                handler(json.template get<Message>());
            });
    }

    std::shared_ptr<PendingQuestion> get_question(const std::string& question_type, bool& available) {
        return inner_->get_question(question_type, available);
    }

    size_t add_tell_received_handler(MessageBox::TellHandler handler) {
        return inner_->add_tell_received_handler(std::move(handler));
    }

    void remove_tell_received_handler(size_t token) { inner_->remove_tell_received_handler(token); }

    size_t add_question_received_handler(MessageBox::QuestionHandler handler) {
        return inner_->add_question_received_handler(std::move(handler));
    }

    void remove_question_received_handler(size_t token) { inner_->remove_question_received_handler(token); }

private:
    std::shared_ptr<MessageBox> inner_;

    template <typename T>
    static nlohmann::json serialize(const T& value) {
        nlohmann::json json_value;
        using nlohmann::to_json;
        to_json(json_value, value);
        return json_value;
    }
};

} // namespace zmesh

