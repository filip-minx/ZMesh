#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <chrono>

namespace minx::zmesh {

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

    void push(T value) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(value));
        }
        cv_.notify_one();
    }

    [[nodiscard]] bool try_pop(T& value) {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] bool wait_pop(T& value, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return closed_ || !queue_.empty(); })) {
            return false;
        }
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    bool closed_{false};
};

} // namespace minx::zmesh
