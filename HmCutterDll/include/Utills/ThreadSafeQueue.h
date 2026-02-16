#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

namespace HmCutter {

    template<typename T>
    class ThreadSafeQueue {
    public:
        explicit ThreadSafeQueue(size_t cap) : cap_(cap) {}

        // push: 오래된 것 버리고 최신을 넣는다
        void push(T&& v) {
            std::lock_guard<std::mutex> lk(m_);
            if (stopped_) return;
            if (q_.size() >= cap_) q_.pop_front();
            q_.push_back(std::move(v));
            cv_.notify_one();
        }

        bool pop(T& out) {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return stopped_ || !q_.empty(); });
            if (q_.empty()) return false;
            out = std::move(q_.front());
            q_.pop_front();
            return true;
        }

        void stop() {
            std::lock_guard<std::mutex> lk(m_);
            stopped_ = true;
            cv_.notify_all();
        }

        size_t size() const {
            std::lock_guard<std::mutex> lk(m_);
            return q_.size();
        }

    private:
        mutable std::mutex m_;
        std::condition_variable cv_;
        std::deque<T> q_;
        size_t cap_;
        bool stopped_ = false;
    };


} // namespace HmCutter
