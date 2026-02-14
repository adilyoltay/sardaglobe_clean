#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace globe {

template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t maxSize)
        : maxSize_(maxSize) {}

    void Close() {
        closed_.store(true);
        notFull_.notify_all();
        notEmpty_.notify_all();
    }

    bool Push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this]() { return queue_.size() < maxSize_ || closed_.load(); });
        if (closed_.load()) return false;
        queue_.push(std::move(item));
        notEmpty_.notify_one();
        return true;
    }
    
    // Non-blocking push - returns immediately with false if queue is full
    bool TryPush(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_.load() || queue_.size() >= maxSize_) {
            return false;
        }
        queue_.push(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    bool TryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        notFull_.notify_one();
        return true;
    }
    
    // Blocking pop - waits until item available or closed
    bool Pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this]() { return !queue_.empty() || closed_.load(); });
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        notFull_.notify_one();
        return true;
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    bool IsClosed() const {
        return closed_.load();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::queue<T> queue_;
    size_t maxSize_ = 0;
    std::atomic<bool> closed_{false};
};

} // namespace globe
