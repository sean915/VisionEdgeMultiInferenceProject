#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>

namespace HmCutter {

    template<typename T>
    class ThreadSafeQueue {
    public:
        explicit ThreadSafeQueue(size_t cap) : cap_(cap) {}

        // push: 오래된 것 버리고 최신을 넣는다
        // 반환값: 드랍된 아이템 수 (0 또는 1)
        void push(T&& v) {
            std::lock_guard<std::mutex> lk(m_);
            if (stopped_) return;
            if (q_.size() >= cap_) {
                q_.pop_front();
                ++drop_count_;
            }
            ++push_count_;
            q_.push_back(std::move(v));
            cv_.notify_one();
        }

        bool pop(T& out) {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return stopped_ || !q_.empty(); });
            if (q_.empty()) return false;
            out = std::move(q_.front());
            q_.pop_front();
            ++pop_count_;
            return true;
        }

        void stop() {
            std::lock_guard<std::mutex> lk(m_);
            stopped_ = true;
            cv_.notify_all();
        }

        /// 큐를 재사용 가능 상태로 되돌린다. stop() 후 다시 push/pop 하려면 호출.
        void reset() {
            std::lock_guard<std::mutex> lk(m_);
            stopped_ = false;
            q_.clear();
            push_count_ = 0;
            pop_count_ = 0;
            drop_count_ = 0;
        }

        size_t size() const {
            std::lock_guard<std::mutex> lk(m_);
            return q_.size();
        }

        // ✅ 디버깅용 통계
        uint64_t push_count() const { std::lock_guard<std::mutex> lk(m_); return push_count_; }
        uint64_t pop_count()  const { std::lock_guard<std::mutex> lk(m_); return pop_count_;  }
        uint64_t drop_count() const { std::lock_guard<std::mutex> lk(m_); return drop_count_; }

    private:
        mutable std::mutex m_;
        std::condition_variable cv_;
        std::deque<T> q_;
        size_t cap_;
        bool stopped_ = false;
        uint64_t push_count_ = 0;
        uint64_t pop_count_  = 0;
        uint64_t drop_count_ = 0;
    };


} // namespace HmCutter
