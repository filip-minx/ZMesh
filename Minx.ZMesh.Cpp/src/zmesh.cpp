#include "zmesh/zmesh.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

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

std::shared_ptr<TypedMessageBox> ZMesh::at(const std::string& name) {
    std::shared_ptr<TypedMessageBox> typed;
    {
        std::lock_guard lock(message_boxes_mutex_);
        typed = ensure_entry(name).typed_box;
    }
    return typed;
}

void ZMesh::router_loop(std::stop_token stop_token) {
    zmq::socket_t router{context_, zmq::socket_type::router};
    router.set(zmq::sockopt::linger, 0);
    router.bind("tcp://" + *address_);

    while (!stop_token.stop_requested()) {
        zmq::pollitem_t items[] = {{static_cast<void*>(router), 0, ZMQ_POLLIN, 0}};
        [[maybe_unused]] const auto poll_result = zmq::poll(items, 1, 50);

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
            const auto payload_str = payload.to_string();
            auto json = nlohmann::json::parse(payload_str);

            switch (message_type) {
            case MessageType::Tell: {
                auto tell = json.get<TellMessage>();
                std::shared_ptr<MessageBox> box;
                {
                    std::lock_guard lock(message_boxes_mutex_);
                    box = ensure_entry(tell.message_box_name).message_box;
                }
                box->write_tell_message(tell);
                break;
            }
            case MessageType::Question: {
                auto question = json.get<QuestionMessage>();
                std::shared_ptr<MessageBox> box;
                {
                    std::lock_guard lock(message_boxes_mutex_);
                    box = ensure_entry(question.message_box_name).message_box;
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

ZMesh::MessageBoxEntry& ZMesh::ensure_entry(const std::string& name) {
    auto it = message_boxes_.find(name);
    if (it == message_boxes_.end()) {
        auto message_box = create_message_box(name);
        auto typed = std::make_shared<TypedMessageBox>(message_box);
        auto [inserted, inserted_success] =
            message_boxes_.emplace(name, MessageBoxEntry{message_box, typed});
        (void)inserted_success;
        return inserted->second;
    }

    if (!it->second.message_box) {
        it->second.message_box = create_message_box(name);
    }

    if (!it->second.typed_box) {
        it->second.typed_box = std::make_shared<TypedMessageBox>(it->second.message_box);
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

        auto json = nlohmann::json(message).dump();
        zmq::message_t identity_frame{identity.begin(), identity.end()};
        zmq::message_t payload_frame{json.begin(), json.end()};

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

