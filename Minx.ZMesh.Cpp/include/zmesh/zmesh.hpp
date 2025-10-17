#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <stop_token>
#include <thread>

#include <zmq.hpp>

#include "message_box.hpp"
#include "typed_message_box.hpp"

namespace zmesh {

class ZMesh {
public:
    ZMesh(std::optional<std::string> address, std::unordered_map<std::string, std::string> system_map);
    ~ZMesh();

    ZMesh(const ZMesh&) = delete;
    ZMesh& operator=(const ZMesh&) = delete;

    std::shared_ptr<TypedMessageBox> at(const std::string& name);

private:
    struct MessageBoxEntry {
        std::shared_ptr<MessageBox> message_box;
        std::shared_ptr<TypedMessageBox> typed_box;
    };

    void router_loop(std::stop_token stop_token);
    MessageBoxEntry& ensure_entry(const std::string& name);
    std::shared_ptr<MessageBox> create_message_box(const std::string& name);
    void enqueue_answer(const std::string& dealer_identity, AnswerMessage message);
    void flush_answers(zmq::socket_t& router_socket);

    zmq::context_t context_{1};
    std::optional<std::string> address_;
    std::unordered_map<std::string, std::string> system_map_;

    std::mutex message_boxes_mutex_;
    std::unordered_map<std::string, MessageBoxEntry> message_boxes_;

    std::mutex answer_mutex_;
    std::queue<std::pair<std::string, AnswerMessage>> pending_answers_;

    std::jthread router_thread_;
};

} // namespace zmesh

