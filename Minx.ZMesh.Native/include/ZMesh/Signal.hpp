#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace zmesh
{
    template <typename... TArgs>
    class Signal
    {
    public:
        class Subscription
        {
        public:
            Subscription() = default;

            Subscription(Signal* parent, std::size_t token) noexcept
                : parent_(parent), token_(token)
            {
            }

            Subscription(const Subscription&) = delete;
            Subscription& operator=(const Subscription&) = delete;

            Subscription(Subscription&& other) noexcept
                : parent_(std::exchange(other.parent_, nullptr)), token_(other.token_)
            {
            }

            Subscription& operator=(Subscription&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    parent_ = std::exchange(other.parent_, nullptr);
                    token_ = other.token_;
                }

                return *this;
            }

            ~Subscription()
            {
                reset();
            }

            void reset() noexcept
            {
                if (parent_)
                {
                    parent_->unsubscribe(token_);
                    parent_ = nullptr;
                }
            }

            explicit operator bool() const noexcept
            {
                return parent_ != nullptr;
            }

        private:
            Signal* parent_ {nullptr};
            std::size_t token_ {0};
        };

        using Handler = std::function<void(const TArgs&...)>;

        Subscription subscribe(Handler handler)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto token = next_token_++;
            handlers_.emplace(token, std::move(handler));
            return Subscription{this, token};
        }

        void emit(const TArgs&... args)
        {
            std::unordered_map<std::size_t, Handler> handlers_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                handlers_copy = handlers_;
            }

            for (auto& [_, handler] : handlers_copy)
            {
                if (handler)
                {
                    handler(args...);
                }
            }
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handlers_.clear();
        }

    private:
        void unsubscribe(std::size_t token)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handlers_.erase(token);
        }

        std::unordered_map<std::size_t, Handler> handlers_;
        std::mutex mutex_;
        std::atomic<std::size_t> next_token_ {1};
    };
}
