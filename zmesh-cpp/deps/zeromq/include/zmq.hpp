#pragma once

// Minimal cppzmq-compatible shim that provides the subset of the API required
// by the bundled ZMesh C++ implementation. The shim delegates to the C ZeroMQ
// headers provided by libzmq and intentionally implements only the features
// used by the library and calculator example.

#include <zmq.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

namespace zmq
{
    class error_t : public std::runtime_error
    {
    public:
        error_t()
            : std::runtime_error(zmq_strerror(zmq_errno())),
              value_(zmq_errno())
        {
        }

        explicit error_t(int value)
            : std::runtime_error(zmq_strerror(value)),
              value_(value)
        {
        }

        int num() const noexcept
        {
            return value_;
        }

    private:
        int value_;
    };

    enum class socket_type : int
    {
        dealer = ZMQ_DEALER,
        router = ZMQ_ROUTER
    };

    enum class send_flags : int
    {
        none = 0,
#ifdef ZMQ_SNDMORE
        sndmore = ZMQ_SNDMORE
#else
        sndmore = 2
#endif
    };

    enum class recv_flags : int
    {
        none = 0,
#ifdef ZMQ_DONTWAIT
        dontwait = ZMQ_DONTWAIT
#else
        dontwait = 1
#endif
    };

    namespace sockopt
    {
#ifdef ZMQ_ROUTING_ID
        constexpr int routing_id_value = ZMQ_ROUTING_ID;
#else
        constexpr int routing_id_value = ZMQ_IDENTITY;
#endif

        struct routing_id_t
        {
            static constexpr int value = routing_id_value;
        };

        inline constexpr routing_id_t routing_id{};
    } // namespace sockopt

    class message_t
    {
    public:
        message_t()
        {
            init();
        }

        explicit message_t(std::size_t size)
        {
            init_size(size);
        }

        ~message_t() noexcept
        {
            close();
        }

        message_t(const message_t&) = delete;
        message_t& operator=(const message_t&) = delete;
        message_t(message_t&&) = delete;
        message_t& operator=(message_t&&) = delete;

        void rebuild()
        {
            close();
            init();
        }

        void rebuild(std::size_t size)
        {
            close();
            init_size(size);
        }

        void* data() noexcept
        {
            return zmq_msg_data(&message_);
        }

        const void* data() const noexcept
        {
            return zmq_msg_data(const_cast<zmq_msg_t*>(&message_));
        }

        std::size_t size() const noexcept
        {
            return zmq_msg_size(const_cast<zmq_msg_t*>(&message_));
        }

        std::string to_string() const
        {
            auto ptr = static_cast<const char*>(data());
            return std::string(ptr, ptr + size());
        }

    private:
        void init()
        {
            if (zmq_msg_init(&message_) != 0)
            {
                throw error_t();
            }

            initialized_ = true;
        }

        void init_size(std::size_t size)
        {
            if (zmq_msg_init_size(&message_, size) != 0)
            {
                throw error_t();
            }

            initialized_ = true;
        }

        void close() noexcept
        {
            if (initialized_)
            {
                zmq_msg_close(&message_);
                initialized_ = false;
            }
        }

        zmq_msg_t message_{};
        bool initialized_{false};

        friend class socket_t;
    };

    class context_t
    {
    public:
        explicit context_t(int io_threads)
        {
            context_ = zmq_ctx_new();
            if (!context_)
            {
                throw error_t();
            }

            if (zmq_ctx_set(context_, ZMQ_IO_THREADS, io_threads) != 0)
            {
                auto err = zmq_errno();
                zmq_ctx_term(context_);
                context_ = nullptr;
                throw error_t(err);
            }
        }

        ~context_t() noexcept
        {
            if (context_)
            {
                zmq_ctx_term(context_);
            }
        }

        context_t(const context_t&) = delete;
        context_t& operator=(const context_t&) = delete;
        context_t(context_t&& other) noexcept
            : context_(other.context_)
        {
            other.context_ = nullptr;
        }

        context_t& operator=(context_t&& other) noexcept
        {
            if (this != &other)
            {
                if (context_)
                {
                    zmq_ctx_term(context_);
                }

                context_ = other.context_;
                other.context_ = nullptr;
            }

            return *this;
        }

        void* handle() const noexcept
        {
            return context_;
        }

    private:
        void* context_{};
    };

    class socket_t
    {
    public:
        socket_t(context_t& context, socket_type type)
        {
            socket_ = zmq_socket(context.handle(), static_cast<int>(type));
            if (!socket_)
            {
                throw error_t();
            }
        }

        ~socket_t() noexcept
        {
            close();
        }

        socket_t(const socket_t&) = delete;
        socket_t& operator=(const socket_t&) = delete;
        socket_t(socket_t&& other) noexcept
            : socket_(other.socket_)
        {
            other.socket_ = nullptr;
        }

        socket_t& operator=(socket_t&& other) noexcept
        {
            if (this != &other)
            {
                close();
                socket_ = other.socket_;
                other.socket_ = nullptr;
            }

            return *this;
        }

        void close() noexcept
        {
            if (socket_)
            {
                zmq_close(socket_);
                socket_ = nullptr;
            }
        }

        void bind(const std::string& endpoint)
        {
            if (zmq_bind(socket_, endpoint.c_str()) != 0)
            {
                throw error_t();
            }
        }

        void connect(const std::string& endpoint)
        {
            if (zmq_connect(socket_, endpoint.c_str()) != 0)
            {
                throw error_t();
            }
        }

        std::size_t send(const message_t& message, send_flags flags = send_flags::none)
        {
            int rc;
            do
            {
                rc = zmq_send(socket_, message.data(), static_cast<int>(message.size()), static_cast<int>(flags));
            } while (rc == -1 && zmq_errno() == EINTR);

            if (rc == -1)
            {
                throw error_t();
            }

            return static_cast<std::size_t>(rc);
        }

        std::optional<std::size_t> recv(message_t& message, recv_flags flags = recv_flags::none)
        {
            message.rebuild();

            int rc;
            do
            {
                rc = zmq_msg_recv(&message.message_, socket_, static_cast<int>(flags));
            } while (rc == -1 && zmq_errno() == EINTR);

            if (rc >= 0)
            {
                return static_cast<std::size_t>(rc);
            }

            if (zmq_errno() == EAGAIN)
            {
                return std::nullopt;
            }

            throw error_t();
        }

        std::optional<std::size_t> recv(message_t& message)
        {
            return recv(message, recv_flags::none);
        }

        void set(sockopt::routing_id_t, const std::string& value)
        {
            set_raw(sockopt::routing_id_t::value, value.data(), value.size());
        }

    private:
        void set_raw(int option, const void* data, std::size_t size)
        {
            if (zmq_setsockopt(socket_, option, data, size) != 0)
            {
                throw error_t();
            }
        }

        void* socket_{};
    };

} // namespace zmq

