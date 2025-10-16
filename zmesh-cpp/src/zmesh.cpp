#include "zmesh/zmesh.hpp"

#include "zmesh/messages.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace zmesh
{
    ZMesh::ZMesh(std::optional<std::string> bind_address, SystemMap system_map)
        : system_map_(std::move(system_map)),
          context_(std::make_shared<zmq::context_t>(1))
    {
        if (bind_address)
        {
            router_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::router);
            router_->bind("tcp://" + *bind_address);

            running_.store(true, std::memory_order_release);
            router_thread_ = std::thread(&ZMesh::router_loop, this);
        }
    }

    ZMesh::~ZMesh()
    {
        running_.store(false, std::memory_order_release);
        answer_cv_.notify_all();

        if (router_)
        {
            try
            {
                router_->close();
            }
            catch (const zmq::error_t&)
            {
            }
        }

        if (router_thread_.joinable())
        {
            router_thread_.join();
        }

        std::lock_guard lock(boxes_mutex_);
        message_boxes_.clear();
    }

    std::shared_ptr<MessageBox> ZMesh::at(const std::string& name)
    {
        {
            std::lock_guard lock(boxes_mutex_);
            auto it = message_boxes_.find(name);
            if (it != message_boxes_.end())
            {
                if (auto existing = it->second.lock())
                {
                    return existing;
                }

                message_boxes_.erase(it);
            }
        }

        auto it = system_map_.find(name);
        if (it == system_map_.end())
        {
            throw std::invalid_argument("Unknown message box name: " + name);
        }

        auto message_box = std::make_shared<MessageBox>(name, it->second, context_);

        {
            std::lock_guard lock(boxes_mutex_);
            message_boxes_[name] = message_box;
        }

        return message_box;
    }

    void ZMesh::router_loop()
    {
        while (running_.load(std::memory_order_acquire))
        {
            flush_pending_answers();

            bool handled_message = false;
            if (router_)
            {
                zmq::message_t identity_frame;
                auto identity_result = router_->recv(identity_frame, zmq::recv_flags::dontwait);
                if (identity_result)
                {
                    zmq::message_t type_frame;
                    if (!router_->recv(type_frame))
                    {
                        continue;
                    }

                    zmq::message_t payload_frame;
                    if (!router_->recv(payload_frame))
                    {
                        continue;
                    }

                    auto identity = identity_frame.to_string();
                    auto type_string = type_frame.to_string();
                    auto payload_json = nlohmann::json::parse(payload_frame.to_string());
                    auto message_type = message_type_from_string(type_string);

                    handle_message(identity, message_type, payload_json);
                    handled_message = true;
                }
            }

            if (!handled_message)
            {
                std::unique_lock lock(answer_mutex_);
                answer_cv_.wait_for(lock, std::chrono::milliseconds(50), [this]
                                    {
                                        return !answer_queue_.empty() || !running_.load(std::memory_order_acquire);
                                    });
            }
        }

        flush_pending_answers();
    }

    void ZMesh::flush_pending_answers()
    {
        if (!router_)
        {
            return;
        }

        std::queue<IdentityAnswer> answers;
        {
            std::lock_guard lock(answer_mutex_);
            std::swap(answers, answer_queue_);
        }

        while (!answers.empty())
        {
            auto answer = std::move(answers.front());
            answers.pop();

            auto payload = answer_message_to_json(answer.message).dump();

            zmq::message_t identity_frame(answer.dealer_identity.size());
            std::memcpy(identity_frame.data(), answer.dealer_identity.data(), answer.dealer_identity.size());
            router_->send(identity_frame, zmq::send_flags::sndmore);

            zmq::message_t payload_frame(payload.size());
            std::memcpy(payload_frame.data(), payload.data(), payload.size());
            router_->send(payload_frame, zmq::send_flags::none);
        }
    }

    void ZMesh::handle_message(const std::string& identity, MessageType type, const nlohmann::json& payload)
    {
        switch (type)
        {
        case MessageType::Tell:
        {
            auto tell = tell_message_from_json(payload);
            auto box = at(tell.message_box_name);
            if (box)
            {
                box->write_tell_message(tell);
            }
            break;
        }
        case MessageType::Question:
        {
            auto question = question_message_from_json(payload);
            auto box = at(question.message_box_name);
            if (box)
            {
                auto pending = std::make_shared<PendingQuestion>(question.message_box_name,
                                                                  [this](IdentityAnswer&& answer)
                                                                  {
                                                                      enqueue_answer(std::move(answer));
                                                                  });
                pending->question_message = question;
                pending->dealer_identity = identity;
                box->write_question_message(std::move(pending));
            }
            break;
        }
        default:
            break;
        }
    }

    void ZMesh::enqueue_answer(IdentityAnswer&& answer)
    {
        {
            std::lock_guard lock(answer_mutex_);
            answer_queue_.push(std::move(answer));
        }
        answer_cv_.notify_one();
    }
}

