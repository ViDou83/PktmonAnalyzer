#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>
#include <utility>
#include <iostream>

struct Statistics {
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> totalPushed{ 0 };
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> totalPopped{ 0 };
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> pushFailures{ 0 };
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

        // Auto-reset event - resets after one thread is released
        hEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!hEvent_) {
            throw std::runtime_error("Failed to create event");
        }
    }

    ~RingBuffer() {
        if (hEvent_) {
            CloseHandle(hEvent_);
        }
    }
  
    // Non-copyable, non-movable (due to HANDLE)
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    bool tryPush(const T& t) {
        return push(t);
    }

    bool tryPush(T&& t) {
        return push(std::move(t));
    }

    // Single producer only
    bool push(T&& v) {
        for (;;) {
            std::size_t h = head_.load(std::memory_order_relaxed);
            const std::size_t t = tail_.load(std::memory_order_acquire); // observe consumer progress

            // full
            if (h - t >= cap_) {
                stats_.pushFailures.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            // Slot still occupied (consumer hasn't cleared it yet)
            if (buf_[h & mask_].ready.load(std::memory_order_acquire)) {
                stats_.pushFailures.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

			// CAS to claim slot 'h'
            if (head_.compare_exchange_weak(h, h + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                buf_[h & mask_].data = std::move(v); 
                buf_[h & mask_].ready.store(true, std::memory_order_release); // publish slot ready
                stats_.totalPushed.fetch_add(1, std::memory_order_relaxed);
                SetEvent(hEvent_); // signal one waiting consumer
                return true;
            }
            // CAS failed: 'h' updated to latest head, loop and retry
        }
    }


    // Multi-consumer
    std::optional<T> tryPop() {
        for (;;) {
            std::size_t t = tail_.load(std::memory_order_relaxed);
            const std::size_t h = head_.load(std::memory_order_acquire);
            
            if (t == h) {
                return std::nullopt; // empty
            }

            if (!buf_[t & mask_].ready.load(std::memory_order_acquire))
            {
                continue;
            }

            // Attempt to claim slot t
            if (tail_.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                T out = std::move(buf_[t & mask_].data);
				buf_[t & mask_].ready.store(false, std::memory_order_release); // mark slot empty
                stats_.totalPopped.fetch_add(1, std::memory_order_relaxed);
                return out;
            }
            // CAS failed: 't' updated to latest tail, loop and retry
        }
    }

    // Blocking pop with timeout (replaces busy-spin)
    std::optional<T> waitPop(DWORD timeoutMs = 50) {
        if (auto result = tryPop()) {
            return result;
        }

        // Wait for producer signal or timeout
        WaitForSingleObject(hEvent_, timeoutMs);

        return tryPop();
    }

    // Wake all waiting consumers (for shutdown)
    void signalAll(const int numberOfThreads) {
        // Manual-reset would be better for broadcast, but we can pulse multiple times
        for (int i = 0; i < numberOfThreads; ++i) {  // Wake up to numberOfThreads waiting threads
            SetEvent(hEvent_);
        }
    }

    std::size_t capacity() const noexcept { 
        return cap_;
    }

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
        alignas(std::hardware_destructive_interference_size) std::atomic<bool> ready{ false };
	};  

    const std::size_t cap_;
    const std::size_t mask_;
    std::vector<slot> buf_;

    // head: only producer writes, consumers read
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> head_{ 0 };
    // tail: consumers contend here (CAS), producer reads
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> tail_{ 0 };
    alignas(std::hardware_destructive_interference_size) Statistics stats_;

    HANDLE hEvent_{ nullptr };

    static_assert(std::atomic<size_t>::is_always_lock_free);
};