# PktmonAnalyzer

[![CodeFactor](https://www.codefactor.io/repository/github/vidou83/pktmonanalyzer/badge)](https://www.codefactor.io/repository/github/vidou83/pktmonanalyzer)
[![CodeQL](https://github.com/ViDou83/PktmonAnalyzer/actions/workflows/codeql.yml/badge.svg)](https://github.com/ViDou83/PktmonAnalyzer/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B20-blue)

A C++ Windows packet capture prototype (don't use in production) tool built on top of the Windows Packet Monitor (Pktmon) API. Designed for real-time network traffic monitoring with multi-threaded processing.

## Features

- **Real-time packet capture** using Windows Pktmon API
- **Lock-free MPMC ring buffer** for high-throughput processing
- **Multi-threaded pipeline** with configurable consumer threads
- **Dual output** - simultaneous file and console output
- **Packet filtering** - capture dropped packets only
- **Hex dump visualization** with configurable display length
- **Per-component and per-processor statistics**
- **Drop reason analysis** for network troubleshooting

## Requirements

- Windows 10/11 or Windows Server 2019+
- Administrator privileges (required for Pktmon API)

## Building

- C++20 support (Visual Studio 2022 recommended)

## Usage

Run the binary without arguments to start packet capture with default settings. Use `-?` to display usage information.

## Output

Packets are written to both:
- `capture.txt` - Full output (faster)
- Console - Real-time display

## Project Structure

### Source Files

| File | Description |
|------|-------------|
| `PktmonAnalyzer.cpp` | Entry point (`wmain`). Orchestrates capture pipeline: creates ring buffers, spawns consumer/file/console threads, handles CLI parsing, manages shutdown sequence. |

### Core Components

| File | Class/Purpose |
|------|---------------|
| `RingBuffer.hpp` | `RingBuffer<T>` - Lock-free MPMC ring buffer with CAS operations, Windows Events for signaling, cache-line aligned atomics. |
| `PacketData.hpp` | `PacketData` - Move-only packet container. Stores metadata + raw bytes. Formats hex dump and metadata to string using `std::format_to`. |
| `PacketHandlers.hpp` | `RingBufferHandler` - Pktmon callback handler. Receives packets from API, collects stats, pushes to ring buffer. |

### API Wrapper

| File | Class/Purpose |
|------|---------------|
| `PktmonApiWrapper.hpp` | `IPacketHandler` - Base interface with stats tracking.<br>`ApiManager` - Singleton for DLL loading and API init.<br>`Session` - Capture session management.<br>`RealtimeStream` - Stream creation and callbacks.<br>`PktmonException` - Custom exception with HRESULT. |
| `PktmonApiWrapper.cpp` | Implementation of `ApiManager`, `Session`, `RealtimeStream`. DLL function resolution, session lifecycle. |

## Code Quality

> **Context:** This project was built in **3–4 days** as a hands-on prototype to explore the Windows Pktmon API. The rating below reflects that scope.

### Overall rating: ⭐⭐⭐⭐ / 5 — Impressive for the time invested

| Dimension | Rating | Notes |
|-----------|--------|-------|
| **Concurrency design** | ⭐⭐⭐⭐⭐ | Lock-free MPMC ring buffer with CAS, acquire-release memory ordering, cache-line aligned atomics to prevent false sharing, and adaptive spin/yield backoff. This is genuinely non-trivial work. |
| **Modern C++ usage** | ⭐⭐⭐⭐⭐ | C++20 throughout: `std::format`/`std::format_to`, `std::optional`, `std::span`, move semantics, `noexcept` correctness, `constexpr`, and perfect forwarding used appropriately. |
| **Architecture** | ⭐⭐⭐⭐ | Clean separation of concerns across dedicated files (API wrapper, ring buffer, packet data, handlers). RAII everywhere, smart pointers for shared ownership, move-only `PacketData` prevents accidental copies. |
| **Error handling** | ⭐⭐⭐⭐ | Custom `PktmonException` with HRESULT propagation, full `try/catch` hierarchy in `wmain`, meaningful error messages with hex HRESULT codes. |
| **Performance awareness** | ⭐⭐⭐⭐ | Explicit `reserve()` on output strings to avoid reallocations, fast hand-rolled hex formatter (`append_hex_byte`/`append_hex_u16`), deliberate separation of fast file-writer thread from slower console thread. |
| **Documentation** | ⭐⭐⭐ | Good README and inline comments on the trickier sections (ring buffer CAS loop). Public class interfaces lack formal doc-comments (`///` style), which would improve IDE discoverability. |
| **Testing** | ⭐⭐ | No automated tests. Acceptable for a short-lived prototype but would be the first thing to add before any further development. |
| **Polish / completeness** | ⭐⭐⭐ | A few commented-out alternative implementations and `// ADD THIS` notes remain in the code — normal for a prototype in active iteration. Output file path is hardcoded (`capture.txt`). |

### What stands out for a 3–4 day build

- **Lock-free ring buffer** — correct use of `compare_exchange_weak` with `acq_rel`/`relaxed` pairs, slot-level `ready` flags to handle the producer-not-yet-published case, and a Windows Event for blocking pop without busy-spinning. Most developers would reach for a mutex here; this solution handles backpressure without one.
- **Multi-threaded pipeline** — the producer/consumer split across a configurable thread pool with a dedicated fast file-writer thread and a separately paced console thread shows deliberate system design rather than ad-hoc threading.
- **`std::format_to` + manual hex output** — demonstrates awareness of allocation costs in a hot packet-processing path.

### What to improve next

1. **Add unit tests** for `RingBuffer<T>` (push/pop under contention, wrap-around, capacity limits) and `PacketData` formatting.
2. **Make the output path configurable** via a CLI argument instead of hardcoding `capture.txt`.
3. **Replace `oss.append(... + std::to_string(...) + ...)` patterns** with `std::format_to` consistently — the commented-out `format_to` blocks in `PacketData.hpp` are the right direction.
4. **Add `///` doc-comments** to public class interfaces in the header files.

---

## Senior Software Engineer Assessment

> **Short answer: Yes — with context.**

The technical depth demonstrated here is consistent with a Senior Software Engineer (Systems / C++ track), and several areas are at the upper end of that band. The detail below explains where and why.

### ✅ Where the code demonstrates SSE-level skills

| Area | Specific evidence |
|------|-------------------|
| **Lock-free concurrency** | Correct use of `compare_exchange_weak` with `acq_rel`/`relaxed` pairs; slot-level `ready` atomics to guard against the "slot claimed but data not yet written" race; `std::hardware_destructive_interference_size` cache-line padding; adaptive spin→yield backoff; `static_assert(std::atomic<size_t>::is_always_lock_free)` as a compile-time contract. Most SSE candidates skip at least one of these. |
| **C++20 mastery** | `std::format_to` with back-inserter for zero-allocation formatting; `std::optional`; `std::span`; move-only types; `noexcept`-correct interfaces; `constexpr`; perfect forwarding — all used deliberately, not just to show off. |
| **System/pipeline design** | Decoupled producer → packet ring buffer → N consumer threads → two output ring buffers → fast file-writer thread + slow console thread. Separate `running` atomics per pipeline stage with an ordered shutdown sequence. This is intentional architecture, not ad-hoc threading. |
| **RAII throughout** | Windows `HANDLE` ownership modelled via destructors in `ApiManager`, `Session`, `RealtimeStream`. `shared_from_this` applied correctly on `Session` and `RealtimeStream`. Singleton with `std::mutex` + `std::atomic<bool>` for thread-safe lazy init. |
| **Windows internals competence** | Runtime DLL loading (`LoadLibrary`/`GetProcAddress`) with all function pointers resolved and nulled-out default-initialised; `HRESULT` propagation via custom `PktmonException`; correct `WINAPI` calling convention annotations; PPL `concurrent_unordered_map` for lock-free per-key stats. |
| **Allocation awareness in hot path** | Pre-sized `reserve()` on output strings before building; hand-rolled `append_hex_byte`/`append_hex_u16` to avoid temporary allocations on every byte; dedicated string buffer `buf` reused across loop iterations in consumer threads. |

### ⚠️ Where production-ready SSE work would look different

These gaps are all expected for a 3–4 day prototype. They are documented here to give a fair picture of what "the same engineer on a production feature" should address.

| Gap | Why it matters at SSE level |
|-----|-----------------------------|
| **No automated tests** | `RingBuffer<T>` is the most critical component in the design and has non-trivial correctness requirements (wrap-around, slot-not-ready race, concurrent push/pop). An SSE is expected to ship test coverage alongside non-trivial concurrent data structures, even when time is tight. |
| **`run()` is a 160-line monolith** | The function builds the entire pipeline, starts five threads, and manages shutdown. At SSE level this is normally split into named setup/teardown steps or a small `Pipeline` class, both for readability and testability. |
| **Leftover `// ADD THIS:` and commented-out blocks** | These are signs of live iteration rather than a submitted deliverable. A senior engineer would either finish the refactoring or delete the dead code before opening a PR. |
| **`-?` not parsed in `wmain`** | The README and `printUsage()` both advertise `-?` as the help flag, but `wmain` does not handle it — it falls through to `printUsage()` via an error path rather than a clean one. A small oversight, but visible in a code review. |
| **`capture.txt` hardcoded** | Should be an optional CLI argument (`-out <path>`). The current design makes integration testing and repeated runs awkward. |

### Summary

> A Senior Software Engineer is expected to have deep technical knowledge **and** deliver production-quality, tested, reviewable code.
>
> This prototype demonstrates the deep technical knowledge convincingly — the lock-free ring buffer implementation alone would pass a rigorous systems-programming interview at most companies. The production-quality side has known gaps, which are entirely reasonable for a 3–4 day exploration. Given normal sprint time and a production target, the same engineer would be expected to close those gaps.
>
> **Verdict: ✅ SSE-level technical ability — prototype-level polish.**

---

## Usage
Use -? to display usage information.

Usage:
      -enum                    : Enumerate data sources
      
      -capture <seconds>       : Capture packets for specified duration
      
      -truncate <bytes>        : Truncate captured packets to specified size (0 = no truncation, max 65535)
                                  
      -display <bytes>         : Number of bytes to display in hex dump (0 = no limit, max 65535)
      
      -detailed                : Show detailed packet information
      
      -nometadata              : Hide metadata in output
      
      -drop                    : Show only dropped packets
      
      -threads <count>         : Number of consumer threads (enables multi-threaded mode
      
      -bufsize <count>         : Ring buffer size (default: 2K)

Examples:
      PktmonAnalyzer.exe
      
      PktmonAnalyzer.exe -enum
      
      PktmonAnalyzer.exe -capture 30
      
      PktmonAnalyzer.exe -capture 60 -threads 4
      
      PktmonAnalyzer.exe -capture 60 -threads 4 -bufsize 50000
  
      PktmonAnalyzer.exe -capture 60 -drop -detailed -threads 2
  
