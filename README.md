# PktmonAnalyzer

A C++ Windows packet capture prototype (don't use in production) tool using built on top of the Windows Packet Monitor (Pktmon) API. Designed for real-time network traffic monitoring with multi-threaded processing.

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

### Utilities & Definitions

| File | Purpose |
|------|---------|
| `PktmonUtils.h` | `CaptureOptions` - CLI options struct.<br>`DataSourceCache` - Thread-safe component name cache.<br>`formatTimestamp()` - FILETIME to string.<br>`wstringToString()` - Wide to UTF-8 conversion. |
| `Pktmonapi.hpp` | Pktmon API structs and constants: `PACKETMONITOR_STREAM_DATA_DESCRIPTOR`, `PACKETMONITOR_STREAM_METADATA`, etc. |
| `PktMonLoc.hpp` | Enums and string converters for drop reasons (`PKTMON_DROP_REASON`), locations (`PKTMON_DROP_LOCATION`), packet types, directions. |