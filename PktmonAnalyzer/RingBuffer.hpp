#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>
#include <utility>

template <class T>
class SpmcRing {
public:
    explicit SpmcRing(std::size_t capacity_pow2)
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
            return false;
        }

        buf_[h & mask_] = std::move(v); // write payload first
        head_.store(h + 1, std::memory_order_release); // publish moving forward
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
                return out;
            }

            // CAS failed: 't' updated to latest tail, loop and retry
        }
    }

    std::size_t capacity() const noexcept { return cap_; }

private:
    const std::size_t cap_;
    const std::size_t mask_;
    std::vector<T> buf_;

    // head: only producer writes, consumers read
    alignas(64) std::atomic<std::size_t> head_{ 0 };

    // tail: consumers contend here (CAS), producer reads
    alignas(64) std::atomic<std::size_t> tail_{ 0 };
};
