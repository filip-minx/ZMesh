#include "ZMesh/ZmqMessageBox.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace zmesh
{
    namespace
    {
        constexpr std::chrono::milliseconds kPollInterval {100};

        std::string generate_correlation_id()
        {
            std::array<unsigned char, 16> bytes {};
            static thread_local std::mt19937_64 rng{std::random_device{}()};
            for (auto& byte : bytes)
            {
                byte = static_cast<unsigned char>(rng() & 0xFFu);
            }

            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (auto byte : bytes)
            {
                stream << std::setw(2) << static_cast<int>(byte);
            }

            return stream.str();
        }

        std::vector<std::string> receive_frames(zmq::socket_t& socket)
        {
            std::vector<std::string> frames;
            while (true)
            {
                zmq::message_t message;
                if (!socket.recv(message))
                {
                    break;
                }

                frames.emplace_back(message.to_string());

                if (!message.more())
                {
                    break;
                }
            }

            return frames;
        }

        void send_frames(zmq::socket_t& socket, const std::vector<std::string>& frames)
        {
            const auto count = frames.size();
            for (std::size_t index = 0; index < count; ++index)
            {
                zmq::message_t frame{frames[index]};
                const auto flags = index + 1 < count ? zmq::send_flags::sndmore : zmq::send_flags::none;
                socket.send(frame, flags);
            }
        }
    }

    ZmqMessageBox::ZmqMessageBox(Options options)
        : options_(std::move(options)),
          context_(1),
          tell_publisher_(context_, zmq::socket_type::pub),
          tell_subscriber_(context_, zmq::socket_type::sub),
          answer_router_(context_, zmq::socket_type::router)
    {
        if (options_.message_box_name.empty())
        {
            throw std::invalid_argument("Message box name must not be empty.");
        }

        if (options_.tell_publish_endpoint.empty() || options_.tell_subscribe_endpoint.empty() ||
            options_.question_endpoint.empty() || options_.answer_endpoint.empty())
        {
            throw std::invalid_argument("All endpoints must be provided.");
        }

        tell_publisher_.set(zmq::sockopt::linger, 0);
        tell_publisher_.connect(options_.tell_publish_endpoint);

        tell_subscriber_.set(zmq::sockopt::linger, 0);
        tell_subscriber_.connect(options_.tell_subscribe_endpoint);
        tell_subscriber_.set(zmq::sockopt::subscribe, "");

        answer_router_.set(zmq::sockopt::linger, 0);
        answer_router_.bind(options_.answer_endpoint);

        tell_thread_ = std::jthread([this](std::stop_token stop_token) { listen_loop(stop_token); });
        question_thread_ = std::jthread([this](std::stop_token stop_token) { question_loop(stop_token); });
    }

    ZmqMessageBox::~ZmqMessageBox()
    {
        if (tell_thread_.joinable())
        {
            tell_thread_.request_stop();
        }

        if (question_thread_.joinable())
        {
            question_thread_.request_stop();
            answer_cv_.notify_all();
        }

        tell_signal_.clear();
        question_signal_.clear();
    }

    IAbstractMessageBox::Subscription ZmqMessageBox::on_question_received(MessageReceivedHandler handler)
    {
        return question_signal_.subscribe(std::move(handler));
    }

    IAbstractMessageBox::Subscription ZmqMessageBox::on_tell_received(MessageReceivedHandler handler)
    {
        return tell_signal_.subscribe(std::move(handler));
    }

    void ZmqMessageBox::tell(const std::string& content_type, const std::string& content)
    {
        if (content_type.empty())
        {
            throw std::invalid_argument("content_type must not be empty.");
        }

        const std::vector<std::string> frames{
            "tell",
            options_.message_box_name,
            content_type,
            content
        };

        send_frames(tell_publisher_, frames);
    }

    bool ZmqMessageBox::try_listen(const std::string& content_type, std::function<void(const std::string&)> handler)
    {
        std::unique_lock lock(tell_mutex_);
        return tell_handlers_.emplace(content_type, std::move(handler)).second;
    }

    std::future<Answer> ZmqMessageBox::ask(const std::string& content_type, const std::string& content)
    {
        return ask_internal(content_type, content, std::nullopt, std::nullopt);
    }

    bool ZmqMessageBox::try_answer(const std::string& question_content_type, std::function<Answer(const std::string&)> handler)
    {
        std::unique_lock lock(question_mutex_);
        return question_handlers_.emplace(question_content_type, std::move(handler)).second;
    }

    std::future<Answer> ZmqMessageBox::ask(const std::string& content_type)
    {
        return ask_internal(content_type, std::string{}, std::nullopt, std::nullopt);
    }

    std::future<Answer> ZmqMessageBox::ask(const std::string& content_type, std::chrono::milliseconds timeout)
    {
        return ask_internal(content_type, std::string{}, timeout, std::nullopt);
    }

    std::future<Answer> ZmqMessageBox::ask(const std::string& content_type, const std::string& content, std::chrono::milliseconds timeout)
    {
        return ask_internal(content_type, content, timeout, std::nullopt);
    }

    std::future<Answer> ZmqMessageBox::ask(const std::string& content_type, std::stop_token cancellation_token)
    {
        return ask_internal(content_type, std::string{}, std::nullopt, cancellation_token);
    }

    std::future<Answer> ZmqMessageBox::ask(const std::string& content_type, const std::string& content, std::stop_token cancellation_token)
    {
        return ask_internal(content_type, content, std::nullopt, cancellation_token);
    }

    std::optional<PendingQuestion> ZmqMessageBox::get_question(const std::string& question_type, bool& available)
    {
        std::lock_guard lock(pending_mutex_);
        auto iterator = std::find_if(pending_questions_.begin(), pending_questions_.end(),
                                     [&question_type](const QuestionContext& context)
                                     {
                                         return context.message.content_type == question_type;
                                     });

        if (iterator == pending_questions_.end())
        {
            available = false;
            return std::nullopt;
        }

        auto context = *iterator;
        pending_questions_.erase(iterator);
        available = true;
        return PendingQuestion{std::move(context.dealer_identity), context.message, context.answer_callback};
    }

    std::future<Answer> ZmqMessageBox::ask_internal(std::string content_type,
                                                    std::string content,
                                                    std::optional<std::chrono::milliseconds> timeout,
                                                    std::optional<std::stop_token> cancellation_token)
    {
        return std::async(std::launch::async,
                          [this,
                           content_type = std::move(content_type),
                           content = std::move(content),
                           timeout,
                           cancellation_token]() mutable
                          {
                              return perform_request(content_type, content, timeout, cancellation_token);
                          });
    }

    Answer ZmqMessageBox::perform_request(const std::string& content_type,
                                          const std::string& content,
                                          const std::optional<std::chrono::milliseconds>& timeout,
                                          const std::optional<std::stop_token>& cancellation_token)
    {
        zmq::socket_t requester{context_, zmq::socket_type::dealer};
        requester.set(zmq::sockopt::linger, 0);
        requester.set(zmq::sockopt::routing_id, options_.message_box_name);
        requester.connect(options_.question_endpoint);

        const auto correlation_id = generate_correlation_id();
        const std::vector<std::string> frames{
            "question",
            options_.message_box_name,
            content_type,
            content,
            correlation_id,
            content_type
        };

        send_frames(requester, frames);

        const auto start = std::chrono::steady_clock::now();
        while (true)
        {
            if (cancellation_token && cancellation_token->stop_requested())
            {
                throw OperationCancelled("The question has been cancelled.");
            }

            auto wait_timeout = kPollInterval;
            if (timeout)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);

                if (elapsed >= *timeout)
                {
                    throw TimeoutError("The question timed out before receiving an answer.");
                }

                wait_timeout = std::min(wait_timeout, *timeout - elapsed);
            }

            zmq::pollitem_t items[] = {
                { static_cast<void*>(requester), 0, ZMQ_POLLIN, 0 }
            };

            const auto rc = zmq::poll(items, 1, wait_timeout.count());
            if (rc > 0 && (items[0].revents & ZMQ_POLLIN))
            {
                const auto frames_response = receive_frames(requester);
                if (frames_response.size() < 5)
                {
                    continue;
                }

                if (frames_response[0] != "answer")
                {
                    continue;
                }

                if (frames_response[4] != correlation_id)
                {
                    continue;
                }

                return Answer{frames_response[2], frames_response[3]};
            }
        }
    }

    void ZmqMessageBox::listen_loop(std::stop_token stop_token)
    {
        while (!stop_token.stop_requested())
        {
            try
            {
                zmq::pollitem_t items[] = {
                    { static_cast<void*>(tell_subscriber_), 0, ZMQ_POLLIN, 0 }
                };

                const auto rc = zmq::poll(items, 1, kPollInterval.count());
                if (rc > 0 && (items[0].revents & ZMQ_POLLIN))
                {
                    const auto frames = receive_frames(tell_subscriber_);
                    handle_tell_message(frames);
                }
            }
            catch (const zmq::error_t&)
            {
                // Swallow transient errors and continue processing.
            }
        }
    }

    void ZmqMessageBox::question_loop(std::stop_token stop_token)
    {
        while (!stop_token.stop_requested())
        {
            try
            {
                zmq::pollitem_t items[] = {
                    { static_cast<void*>(answer_router_), 0, ZMQ_POLLIN, 0 }
                };

                const auto rc = zmq::poll(items, 1, kPollInterval.count());
                if (rc > 0 && (items[0].revents & ZMQ_POLLIN))
                {
                    auto frames = receive_frames(answer_router_);
                    if (frames.size() < 2)
                    {
                        continue;
                    }

                    const auto dealer_identity = frames.front();
                    frames.erase(frames.begin());

                    if (!frames.empty() && frames.front().empty())
                    {
                        frames.erase(frames.begin());
                    }

                    handle_question_message(dealer_identity, frames);
                }

                drain_pending_answers();

                if (rc == 0)
                {
                    std::unique_lock lock(answer_mutex_);
                    answer_cv_.wait_for(lock, kPollInterval, [&]
                    {
                        return stop_token.stop_requested() || !pending_answers_.empty();
                    });
                }
            }
            catch (const zmq::error_t&)
            {
                // Ignore transient socket errors and continue.
            }
        }

        drain_pending_answers();
    }

    void ZmqMessageBox::handle_tell_message(const std::vector<std::string>& frames)
    {
        if (frames.size() < 4)
        {
            return;
        }

        if (frames[0] != "tell")
        {
            return;
        }

        const auto& content_type = frames[2];
        const auto& content = frames[3];

        MessageReceivedEventArgs args{content_type};
        tell_signal_.emit(args);

        std::shared_lock lock(tell_mutex_);
        const auto iterator = tell_handlers_.find(content_type);
        if (iterator != tell_handlers_.end() && iterator->second)
        {
            iterator->second(content);
        }
    }

    void ZmqMessageBox::handle_question_message(const std::string& dealer_identity, const std::vector<std::string>& frames)
    {
        if (frames.size() < 5)
        {
            return;
        }

        if (frames[0] != "question")
        {
            return;
        }

        QuestionMessage question{
            .message_box_name = frames[1],
            .content_type = frames[2],
            .content = frames[3],
            .correlation_id = frames[4],
            .answer_content_type = frames.size() > 5 ? frames[5] : std::string{}
        };

        auto callback = std::make_shared<std::function<void(const Answer&)>>(
            [this, dealer_identity, message_box_name = question.message_box_name, correlation_id = question.correlation_id](const Answer& answer)
            {
                enqueue_answer(dealer_identity, message_box_name, correlation_id, answer);
            });

        {
            std::lock_guard lock(pending_mutex_);
            pending_questions_.push_back(QuestionContext{dealer_identity, question, callback});
        }

        MessageReceivedEventArgs args{question.content_type};
        question_signal_.emit(args);

        std::function<Answer(const std::string&)> handler;
        {
            std::shared_lock lock(question_mutex_);
            const auto iterator = question_handlers_.find(question.content_type);
            if (iterator != question_handlers_.end())
            {
                handler = iterator->second;
            }
        }

        if (handler)
        {
            const auto answer = handler(question.content);
            (*callback)(answer);
            remove_question_by_correlation(question.correlation_id);
        }
    }

    void ZmqMessageBox::enqueue_answer(std::string dealer_identity,
                                       std::string message_box_name,
                                       std::string correlation_id,
                                       Answer answer)
    {
        PendingAnswer pending_answer{
            .dealer_identity = std::move(dealer_identity),
            .message_box_name = std::move(message_box_name),
            .correlation_id = std::move(correlation_id),
            .answer = std::move(answer)
        };

        {
            std::lock_guard lock(answer_mutex_);
            pending_answers_.push(std::move(pending_answer));
        }

        answer_cv_.notify_one();
    }

    void ZmqMessageBox::drain_pending_answers()
    {
        std::queue<PendingAnswer> answers;
        {
            std::lock_guard lock(answer_mutex_);
            if (pending_answers_.empty())
            {
                return;
            }

            answers.swap(pending_answers_);
        }

        while (!answers.empty())
        {
            send_answer(answers.front());
            answers.pop();
        }
    }

    void ZmqMessageBox::send_answer(const PendingAnswer& pending_answer)
    {
        try
        {
            zmq::message_t identity{pending_answer.dealer_identity};
            answer_router_.send(identity, zmq::send_flags::sndmore);

            zmq::message_t delimiter;
            answer_router_.send(delimiter, zmq::send_flags::sndmore);

            const std::vector<std::string> frames{
                "answer",
                pending_answer.message_box_name,
                pending_answer.answer.content_type,
                pending_answer.answer.content,
                pending_answer.correlation_id
            };

            send_frames(answer_router_, frames);
        }
        catch (const zmq::error_t&)
        {
            // Intentionally ignore socket errors while sending answers.
        }
    }

    void ZmqMessageBox::remove_question_by_correlation(const std::string& correlation_id)
    {
        std::lock_guard lock(pending_mutex_);
        const auto iterator = std::find_if(pending_questions_.begin(), pending_questions_.end(),
                                           [&correlation_id](const QuestionContext& context)
                                           {
                                               return context.message.correlation_id == correlation_id;
                                           });

        if (iterator != pending_questions_.end())
        {
            pending_questions_.erase(iterator);
        }
    }
}
