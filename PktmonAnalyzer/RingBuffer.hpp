#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>
#include <utility>

struct Statistics {
    std::atomic<uint64_t> totalPushed{ 0 };
    std::atomic<uint64_t> totalPopped{ 0 };
    std::atomic<uint64_t> pushFailures{ 0 };
};

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity_pow2)
        : cap_(capacity_pow2),
        mask_(capacity_pow2 - 1),
        buf_(capacity_pow2)
    {
        // Require power of two for fast wrap (idx & mask)
        if ((cap_ & mask_) != 0 || cap_ == 0) {
            throw std::invalid_argument("capacity must be power of two and > 0");
        }
    }

    // Single producer only
    bool tryPush(T&& v) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_acquire); // observe consumer progress
        if (h - t >= cap_) {
            // full
            stats_.pushFailures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        buf_[h & mask_] = std::move(v); // write payload first
        head_.store(h + 1, std::memory_order_release); // publish moving forward
        
		//std::cout << "Producer " << std::this_thread::get_id() << ": Pushed packet to slot " << (h & mask_) << ", new head index: " << (h + 1) << "\n";
        stats_.totalPushed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Multi-consumer
    std::optional<T> tryPop() {
        std::size_t t = tail_.load(std::memory_order_relaxed);
        for (;;) {
            const std::size_t h = head_.load(std::memory_order_acquire);
            if (t == h) {
                return std::nullopt; // empty
            }

            // Attempt to claim slot t
            if (tail_.compare_exchange_weak(
                t, t + 1,
                std::memory_order_acq_rel,
                std::memory_order_relaxed))
            {
                // We own slot t now
                T out = std::move(buf_[t & mask_]);
                stats_.totalPopped.fetch_add(1, std::memory_order_relaxed);
                return out;
            }
            // CAS failed: 't' updated to latest tail, loop and retry
        }
    }

    std::size_t capacity() const noexcept { return cap_; }

    void printStatistics() const {
        std::cout << "RingBuffer Statistics:\n";
        std::cout << "  Capacity:       " << cap_ << "\n";
        std::cout << "  Total Pushed:   " << stats_.totalPushed.load() << "\n";
        std::cout << "  Total Popped:   " << stats_.totalPopped.load() << "\n";
        std::cout << "  Push Failures:  " << stats_.pushFailures.load() << "\n";
	}

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    const std::size_t cap_;
    const std::size_t mask_;
    std::vector<T> buf_;

    // head: only producer writes, consumers read
    alignas(64) std::atomic<std::size_t> head_{ 0 };

    // tail: consumers contend here (CAS), producer reads
    alignas(64) std::atomic<std::size_t> tail_{ 0 };

    alignas(64) Statistics stats_;
};