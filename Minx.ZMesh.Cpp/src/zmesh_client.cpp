#include "zmesh/zmesh_client.hpp"

#include <stdexcept>
#include <string_view>
#include <utility>

namespace zmesh {

namespace {
constexpr auto DEALER_TYPE = zmq::socket_type::dealer;
}

ZMeshClient::ZMeshClient(std::string endpoint, std::string message_box_name, std::optional<std::string> identity)
    : endpoint_(std::move(endpoint)),
      message_box_name_(std::move(message_box_name)),
      identity_(identity.value_or(uuid::generate())) {
    ensure_socket();
}

ZMeshClient::~ZMeshClient() {
    close_socket();
}

ZMeshClient::ZMeshClient(ZMeshClient&& other) noexcept
    : context_(1) {
    std::scoped_lock lock(other.socket_mutex_);
    endpoint_ = std::move(other.endpoint_);
    message_box_name_ = std::move(other.message_box_name_);
    identity_ = std::move(other.identity_);

    if (other.socket_.has_value()) {
        socket_.emplace(std::move(other.socket_.value()));
        other.socket_.reset();
    }
}

ZMeshClient& ZMeshClient::operator=(ZMeshClient&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    std::scoped_lock lock(socket_mutex_, other.socket_mutex_);

    close_socket();

    endpoint_ = std::move(other.endpoint_);
    message_box_name_ = std::move(other.message_box_name_);
    identity_ = std::move(other.identity_);

    if (other.socket_.has_value()) {
        socket_.emplace(std::move(other.socket_.value()));
        other.socket_.reset();
    }

    return *this;
}

AnswerMessage ZMeshClient::ask(const std::string& content_type,
                               const nlohmann::json& payload,
                               RequestOptions options) {
    if (options.max_retries <= 0) {
        throw std::invalid_argument("max_retries must be greater than zero");
    }

    QuestionMessage question;
    question.content_type = content_type;
    question.content = payload.dump();
    question.message_box_name = message_box_name_;
    question.correlation_id = uuid::generate();

    auto question_json = nlohmann::json(question);

    std::scoped_lock lock(socket_mutex_);
    ensure_socket();

    for (int attempt = 0; attempt < options.max_retries; ++attempt) {
        send_message(MessageType::Question, question_json);

        if (auto response = receive_answer(options.timeout)) {
            if (response->correlation_id != question.correlation_id) {
                throw std::runtime_error("Received response with unexpected correlation id");
            }
            return *response;
        }
    }

    throw std::runtime_error("ZMesh request timed out after " + std::to_string(options.max_retries) + " attempts");
}

void ZMeshClient::tell(const std::string& content_type, const nlohmann::json& payload) {
    TellMessage tell;
    tell.content_type = content_type;
    tell.content = payload.dump();
    tell.message_box_name = message_box_name_;

    std::scoped_lock lock(socket_mutex_);
    ensure_socket();
    send_message(MessageType::Tell, nlohmann::json(tell));
}

void ZMeshClient::ensure_socket() {
    if (socket_.has_value()) {
        return;
    }

    socket_.emplace(context_, DEALER_TYPE);
    socket_->set(zmq::sockopt::rcvtimeo, 0);
    socket_->set(zmq::sockopt::linger, 0);
    socket_->set(zmq::sockopt::routing_id, identity_);
    socket_->connect("tcp://" + endpoint_);
}

void ZMeshClient::close_socket() {
    if (!socket_.has_value()) {
        return;
    }

    socket_->close();
    socket_.reset();
}

void ZMeshClient::send_message(MessageType type, const nlohmann::json& payload_json) {
    const auto payload = payload_json.dump();
    const auto type_string = std::string{to_string(type)};

    zmq::message_t type_frame{type_string.begin(), type_string.end()};
    zmq::message_t payload_frame{payload.begin(), payload.end()};

    socket_->send(type_frame, zmq::send_flags::sndmore);
    socket_->send(payload_frame, zmq::send_flags::none);
}

std::optional<AnswerMessage> ZMeshClient::receive_answer(std::chrono::milliseconds timeout) {
    socket_->set(zmq::sockopt::rcvtimeo, static_cast<int>(timeout.count()));

    zmq::message_t reply;
    const auto received = socket_->recv(reply, zmq::recv_flags::none);
    socket_->set(zmq::sockopt::rcvtimeo, 0);

    if (!received) {
        return std::nullopt;
    }

    const auto payload = std::string_view(static_cast<const char*>(reply.data()), reply.size());
    auto json = nlohmann::json::parse(payload.begin(), payload.end());
    return json.get<AnswerMessage>();
}

} // namespace zmesh

