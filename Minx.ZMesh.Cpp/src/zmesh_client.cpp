#include "zmesh/zmesh_client.hpp"

#include <cstring>
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
                               const std::string& payload,
                               RequestOptions options) {
    if (options.max_retries <= 0) {
        throw std::invalid_argument("max_retries must be greater than zero");
    }

    QuestionMessage question;
    question.content_type = content_type;
    question.content = payload;
    question.message_box_name = message_box_name_;
    question.correlation_id = uuid::generate();

    const auto question_payload = serialize_question_message(question);

    std::scoped_lock lock(socket_mutex_);
    ensure_socket();

    for (int attempt = 0; attempt < options.max_retries; ++attempt) {
        send_message(MessageType::Question, question_payload);

        if (auto response = receive_answer(options.timeout)) {
            if (response->correlation_id != question.correlation_id) {
                throw std::runtime_error("Received response with unexpected correlation id");
            }
            return *response;
        }
    }

    throw std::runtime_error("ZMesh request timed out after " + std::to_string(options.max_retries) + " attempts");
}

void ZMeshClient::tell(const std::string& content_type, const std::string& payload) {
    TellMessage tell;
    tell.content_type = content_type;
    tell.content = payload;
    tell.message_box_name = message_box_name_;

    std::scoped_lock lock(socket_mutex_);
    ensure_socket();
    send_message(MessageType::Tell, serialize_tell_message(tell));
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

void ZMeshClient::send_message(MessageType type, const std::string& payload) {
    const auto type_string = std::string{to_string(type)};

    zmq::message_t type_frame{type_string.begin(), type_string.end()};
    zmq::message_t payload_frame{payload.size()};
    if (!payload.empty()) {
        std::memcpy(payload_frame.data(), payload.data(), payload.size());
    }

    const auto type_sent = socket_->send(type_frame, zmq::send_flags::sndmore);
    if (!type_sent) {
        throw std::runtime_error("failed to send message type frame");
    }

    const auto payload_sent = socket_->send(payload_frame, zmq::send_flags::none);
    if (!payload_sent) {
        throw std::runtime_error("failed to send message payload frame");
    }
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
    return deserialize_answer_message(payload);
}

} // namespace zmesh

