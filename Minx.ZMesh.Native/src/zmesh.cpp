#include "minx/zmesh/zmesh.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

#include "minx/zmesh/abstract_message_box.hpp"
#include "minx/zmesh/pending_question.hpp"
#include "minx/zmesh/types.hpp"

namespace minx::zmesh {

ZMesh::ZMesh(std::optional<std::string> address,
             std::unordered_map<std::string, std::string> system_map)
    : context_(1),
      system_map_(std::move(system_map)),
      answer_queue_(std::make_shared<AnswerQueue>()) {
    if (address && !address->empty()) {
        router_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::router);
        router_->set(zmq::sockopt::linger, 0);
        router_->bind("tcp://" + *address);
        router_thread_ = std::jthread([this](std::stop_token stop_token) { RouterLoop(stop_token); });
    }
}

ZMesh::~ZMesh() {
    if (router_thread_.joinable()) {
        router_thread_.request_stop();
        answer_queue_->close();
        router_thread_.join();
    }

    if (router_) {
        try {
            router_->close();
        } catch (...) {
        }
    }

    std::lock_guard lock(message_boxes_mutex_);
    for (auto& [name, weak_box] : message_boxes_) {
        if (auto box = weak_box.lock()) {
            box.reset();
        }
    }
    message_boxes_.clear();
}

std::shared_ptr<IAbstractMessageBox> ZMesh::At(const std::string& name) {
    std::lock_guard lock(message_boxes_mutex_);
    auto it = message_boxes_.find(name);
    if (it != message_boxes_.end()) {
        if (auto existing = it->second.lock()) {
            return existing;
        }
    }

    auto map_it = system_map_.find(name);
    if (map_it == system_map_.end()) {
        throw std::invalid_argument("Unknown message box: " + name);
    }

    auto message_box = std::make_shared<AbstractMessageBox>(name, map_it->second, context_, answer_queue_);
    message_boxes_[name] = message_box;
    return message_box;
}

void ZMesh::RouterLoop(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        if (router_) {
            zmq::pollitem_t items[] = {{*router_, 0, ZMQ_POLLIN, 0}};
            zmq::poll(items, 1, std::chrono::milliseconds{10});

            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t identity_frame;
                zmq::message_t message_box_name_frame;
                zmq::message_t message_type_frame;
                zmq::message_t correlation_frame;
                zmq::message_t content_type_frame;
                zmq::message_t content_frame;

                router_->recv(identity_frame, zmq::recv_flags::none);
                router_->recv(message_box_name_frame, zmq::recv_flags::none);
                router_->recv(message_type_frame, zmq::recv_flags::none);
                router_->recv(correlation_frame, zmq::recv_flags::none);
                router_->recv(content_type_frame, zmq::recv_flags::none);
                router_->recv(content_frame, zmq::recv_flags::none);

                const std::string dealer_identity(static_cast<char*>(identity_frame.data()), identity_frame.size());
                const std::string message_box_name(static_cast<char*>(message_box_name_frame.data()), message_box_name_frame.size());
                const std::string message_type(static_cast<char*>(message_type_frame.data()), message_type_frame.size());
                const std::string correlation_id(static_cast<char*>(correlation_frame.data()), correlation_frame.size());
                const std::string content_type(static_cast<char*>(content_type_frame.data()), content_type_frame.size());
                const std::string content(static_cast<char*>(content_frame.data()), content_frame.size());

                if (message_type == to_string(MessageType::Tell)) {
                    DispatchTell(message_box_name, content_type, content);
                } else if (message_type == to_string(MessageType::Question)) {
                    DispatchQuestion(dealer_identity, message_box_name, correlation_id, content_type, content);
                }
            }
        }

        SendPendingAnswers();

        if (stop_token.stop_requested()) {
            break;
        }
    }
}

void ZMesh::DispatchTell(const std::string& message_box_name,
                         const std::string& content_type,
                         const std::string& content) {
    std::shared_ptr<AbstractMessageBox> message_box;
    {
        std::lock_guard lock(message_boxes_mutex_);
        auto it = message_boxes_.find(message_box_name);
        if (it != message_boxes_.end()) {
            message_box = it->second.lock();
        }
    }

    if (!message_box) {
        message_box = std::dynamic_pointer_cast<AbstractMessageBox>(At(message_box_name));
    }

    if (message_box) {
        message_box->ReceiveTell(TellMessage{.message_box_name = message_box_name,
                                             .content_type = content_type,
                                             .content = content});
    }
}

void ZMesh::DispatchQuestion(const std::string& dealer_identity,
                             const std::string& message_box_name,
                             const std::string& correlation_id,
                             const std::string& content_type,
                             const std::string& content) {
    std::shared_ptr<AbstractMessageBox> message_box;
    {
        std::lock_guard lock(message_boxes_mutex_);
        auto it = message_boxes_.find(message_box_name);
        if (it != message_boxes_.end()) {
            message_box = it->second.lock();
        }
    }

    if (!message_box) {
        message_box = std::dynamic_pointer_cast<AbstractMessageBox>(At(message_box_name));
    }

    if (message_box) {
        PendingQuestion pending_question{.dealer_identity = dealer_identity,
                                         .question_message = QuestionMessage{.message_box_name = message_box_name,
                                                                              .correlation_id = correlation_id,
                                                                              .content_type = content_type,
                                                                              .content = content},
                                         .answer_queue = answer_queue_};
        message_box->ReceiveQuestion(pending_question);
    }
}

void ZMesh::SendPendingAnswers() {
    if (!router_) {
        return;
    }

    IdentityMessage<AnswerMessage> identity_message;
    while (answer_queue_->try_pop(identity_message)) {
        router_->send(zmq::buffer(identity_message.dealer_identity), zmq::send_flags::sndmore);
        router_->send(zmq::buffer(std::string(to_string(MessageType::Answer))), zmq::send_flags::sndmore);
        router_->send(zmq::buffer(identity_message.message.message_box_name), zmq::send_flags::sndmore);
        router_->send(zmq::buffer(identity_message.message.correlation_id), zmq::send_flags::sndmore);
        router_->send(zmq::buffer(identity_message.message.content_type), zmq::send_flags::sndmore);
        router_->send(zmq::buffer(identity_message.message.content), zmq::send_flags::none);
    }
}

} // namespace minx::zmesh
