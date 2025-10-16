#pragma once

#include "message_box.hpp"
#include "typed_message_box.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

namespace zmesh
{
    class ZMesh
    {
    public:
        using SystemMap = std::unordered_map<std::string, std::string>;

        ZMesh(std::optional<std::string> bind_address, SystemMap system_map);
        ~ZMesh();

        ZMesh(const ZMesh&) = delete;
        ZMesh& operator=(const ZMesh&) = delete;

        std::shared_ptr<MessageBox> at(const std::string& name);

        template <typename Serializer>
        std::shared_ptr<TypedMessageBox<Serializer>> at(const std::string& name, Serializer serializer)
        {
            return std::make_shared<TypedMessageBox<Serializer>>(at(name), std::move(serializer));
        }

    private:
        void router_loop();
        void flush_pending_answers();
        void handle_message(const std::string& identity, MessageType type, const nlohmann::json& payload);
        void enqueue_answer(IdentityAnswer&& answer);

        SystemMap system_map_;
        std::shared_ptr<zmq::context_t> context_;
        std::unique_ptr<zmq::socket_t> router_;

        std::atomic<bool> running_{false};
        std::thread router_thread_;

        std::mutex boxes_mutex_;
        std::unordered_map<std::string, std::weak_ptr<MessageBox>> message_boxes_;

        std::mutex answer_mutex_;
        std::queue<IdentityAnswer> answer_queue_;
        std::condition_variable answer_cv_;
    };
}

