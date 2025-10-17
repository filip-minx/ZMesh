#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <typeinfo>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include "messages.hpp"
#include "request_options.hpp"
#include "uuid.hpp"

namespace zmesh {

class ZMeshClient {
public:
    ZMeshClient(std::string endpoint, std::string message_box_name, std::optional<std::string> identity = std::nullopt);
    ~ZMeshClient();

    ZMeshClient(ZMeshClient&& other) noexcept;
    ZMeshClient& operator=(ZMeshClient&& other) noexcept;

    ZMeshClient(const ZMeshClient&) = delete;
    ZMeshClient& operator=(const ZMeshClient&) = delete;

    [[nodiscard]] const std::string& endpoint() const noexcept { return endpoint_; }
    [[nodiscard]] const std::string& message_box_name() const noexcept { return message_box_name_; }
    [[nodiscard]] const std::string& identity() const noexcept { return identity_; }

    AnswerMessage ask(const std::string& content_type,
                      const nlohmann::json& payload,
                      RequestOptions options = {});

    template <typename Question, typename Answer>
    Answer ask(const Question& question, RequestOptions options = {}) {
        auto serialized_question = nlohmann::json(question);
        auto response = ask(typeid(Question).name(), serialized_question, options);
        auto json = nlohmann::json::parse(response.content);
        return json.get<Answer>();
    }

    void tell(const std::string& content_type, const nlohmann::json& payload);

    template <typename Message>
    void tell(const Message& value) {
        tell(typeid(Message).name(), nlohmann::json(value));
    }

private:
    void ensure_socket();
    void close_socket();
    void send_message(MessageType type, const nlohmann::json& payload_json);
    std::optional<AnswerMessage> receive_answer(std::chrono::milliseconds timeout);

    zmq::context_t context_{1};
    std::optional<zmq::socket_t> socket_{};
    std::string endpoint_;
    std::string message_box_name_;
    std::string identity_;
    std::mutex socket_mutex_;
};

} // namespace zmesh

