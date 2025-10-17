#include "zmesh/zmesh.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace zmesh {

ZMesh::ZMesh(std::optional<std::string> address, std::unordered_map<std::string, std::string> system_map)
    : address_{std::move(address)}, system_map_{std::move(system_map)} {
    if (address_) {
        router_thread_ = std::jthread([this](std::stop_token stop_token) { router_loop(std::move(stop_token)); });
    }
}

ZMesh::~ZMesh() {
    if (router_thread_.joinable()) {
        router_thread_.request_stop();
        router_thread_.join();
    }
}

std::shared_ptr<MessageBox> ZMesh::at(const std::string& name) {
    std::lock_guard lock(message_boxes_mutex_);
    return ensure_message_box(name);
}

void ZMesh::router_loop(std::stop_token stop_token) {
    zmq::socket_t router{context_, zmq::socket_type::router};
    router.set(zmq::sockopt::linger, 0);
    router.bind("tcp://" + *address_);

    constexpr auto ROUTER_POLL_INTERVAL = std::chrono::milliseconds(50);

    while (!stop_token.stop_requested()) {
        zmq::pollitem_t items[] = {{static_cast<void*>(router), 0, ZMQ_POLLIN, 0}};
        [[maybe_unused]] const auto poll_result = zmq::poll(items, 1, ROUTER_POLL_INTERVAL);

        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t identity;
            zmq::message_t type_frame;
            zmq::message_t payload;

            if (!router.recv(identity, zmq::recv_flags::none)) {
                continue;
            }

            if (!router.recv(type_frame, zmq::recv_flags::none)) {
                continue;
            }

            if (!router.recv(payload, zmq::recv_flags::none)) {
                continue;
            }

            const auto identity_str = identity.to_string();
            const auto type_str = type_frame.to_string();
            const auto message_type = message_type_from_string(type_str);
            const auto payload_view = std::string_view(static_cast<const char*>(payload.data()), payload.size());

            switch (message_type) {
            case MessageType::Tell: {
                auto tell = deserialize_tell_message(payload_view);
                std::shared_ptr<MessageBox> box;
                {
                    std::lock_guard lock(message_boxes_mutex_);
                    box = ensure_message_box(tell.message_box_name);
                }
                box->write_tell_message(tell);
                break;
            }
            case MessageType::Question: {
                auto question = deserialize_question_message(payload_view);
                std::shared_ptr<MessageBox> box;
                {
                    std::lock_guard lock(message_boxes_mutex_);
                    box = ensure_message_box(question.message_box_name);
                }
                box->write_question_message(question, identity_str);
                break;
            }
            default:
                throw std::invalid_argument("Unexpected message type for router");
            }
        }

        flush_answers(router);
    }

    flush_answers(router);
}

std::shared_ptr<MessageBox> ZMesh::ensure_message_box(const std::string& name) {
    auto it = message_boxes_.find(name);
    if (it == message_boxes_.end()) {
        auto message_box = create_message_box(name);
        auto [inserted, inserted_success] = message_boxes_.emplace(name, std::move(message_box));
        (void)inserted_success;
        return inserted->second;
    }

    if (!it->second) {
        it->second = create_message_box(name);
    }

    return it->second;
}

std::shared_ptr<MessageBox> ZMesh::create_message_box(const std::string& name) {
    auto map_it = system_map_.find(name);
    if (map_it == system_map_.end()) {
        throw std::out_of_range("No system map entry for message box: " + name);
    }

    auto answer_sender = [this](const std::string& dealer_identity, AnswerMessage message) {
        enqueue_answer(dealer_identity, std::move(message));
    };

    return std::make_shared<MessageBox>(name, map_it->second, context_, answer_sender);
}

void ZMesh::enqueue_answer(const std::string& dealer_identity, AnswerMessage message) {
    std::lock_guard lock(answer_mutex_);
    pending_answers_.emplace(dealer_identity, std::move(message));
}

void ZMesh::flush_answers(zmq::socket_t& router_socket) {
    std::queue<std::pair<std::string, AnswerMessage>> answers;
    {
        std::lock_guard lock(answer_mutex_);
        std::swap(answers, pending_answers_);
    }

    while (!answers.empty()) {
        auto [identity, message] = std::move(answers.front());
        answers.pop();

        const auto payload = serialize_answer_message(message);
        zmq::message_t identity_frame{identity.begin(), identity.end()};
        zmq::message_t payload_frame{payload.size()};
        if (!payload.empty()) {
            std::memcpy(payload_frame.data(), payload.data(), payload.size());
        }

        const auto identity_sent = router_socket.send(identity_frame, zmq::send_flags::sndmore);
        if (!identity_sent) {
            throw std::runtime_error("failed to send answer identity frame");
        }

        const auto payload_sent = router_socket.send(payload_frame, zmq::send_flags::none);
        if (!payload_sent) {
            throw std::runtime_error("failed to send answer payload frame");
        }
    }
}

} // namespace zmesh

