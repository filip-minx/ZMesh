#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <zmq.hpp>

#include "abstract_message_box.hpp"

namespace minx::zmesh {

class ZMesh {
public:
    ZMesh(std::optional<std::string> address,
          std::unordered_map<std::string, std::string> system_map);
    ~ZMesh();

    std::shared_ptr<IAbstractMessageBox> At(const std::string& name);

private:
    void RouterLoop(std::stop_token stop_token);
    void DispatchTell(const std::string& message_box_name,
                      const std::string& content_type,
                      const std::string& content);
    void DispatchQuestion(const std::string& dealer_identity,
                          const std::string& message_box_name,
                          const std::string& correlation_id,
                          const std::string& content_type,
                          const std::string& content);
    void SendPendingAnswers();

    zmq::context_t context_;
    std::unordered_map<std::string, std::string> system_map_;

    std::shared_ptr<AnswerQueue> answer_queue_;

    std::unique_ptr<zmq::socket_t> router_;
    std::jthread router_thread_;

    std::mutex message_boxes_mutex_;
    std::unordered_map<std::string, std::weak_ptr<AbstractMessageBox>> message_boxes_;
};

} // namespace minx::zmesh
