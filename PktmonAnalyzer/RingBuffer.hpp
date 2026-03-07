#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>
#include <utility>
#include <iostream>

struct Statistics {
    alignas(64) std::atomic<uint64_t> totalPushed{ 0 };
    alignas(64) std::atomic<uint64_t> totalPopped{ 0 };
    alignas(64) std::atomic<uint64_t> pushFailures{ 0 };
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
        if (h - t >= cap_ || buf_[h & mask_].ready.load(std::memory_order_relaxed)) {
            // full
            stats_.pushFailures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        buf_[h & mask_].data = std::move(v); // write payload first
		buf_[h & mask_].ready.store(true, std::memory_order_release); // publish slot ready
        head_.store(h + 1, std::memory_order_release); // publish moving forward
        
		//std::cout << "Producer " << std::this_thread::get_id() << ": Pushed packet to slot " << (h & mask_) << ", new head index: " << (h + 1) << "\n";
        stats_.totalPushed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Multi-consumer
    std::optional<T> tryPop() {
        for (;;) {
            std::size_t t = tail_.load(std::memory_order_relaxed);
            const std::size_t h = head_.load(std::memory_order_acquire);
            if (t == h) {
                return std::nullopt; // empty
            }
            // Attempt to claim slot t
            if (buf_[t & mask_].ready.load(std::memory_order_acquire) && tail_.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                T out = std::move(buf_[t & mask_].data);
				buf_[t & mask_].ready.store(false, std::memory_order_relaxed); // mark slot empty
                stats_.totalPopped.fetch_add(1, std::memory_order_relaxed);
                return out;
            } else {
                // Slot not ready yet, even though h > t. 
                // Yield to let the producer finish its store.
                std::this_thread::yield();
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
    struct slot 
        {
        T data;
        alignas(64) std::atomic<bool> ready{ false };
	};  

    const std::size_t cap_;
    const std::size_t mask_;
    std::vector<slot> buf_;

    // head: only producer writes, consumers read
    alignas(64) std::atomic<std::size_t> head_{ 0 };
    // tail: consumers contend here (CAS), producer reads
    alignas(64) std::atomic<std::size_t> tail_{ 0 };
    alignas(64) Statistics stats_;
};