# PktmonAnalyzer

A C++ Windows packet capture small tool using built on top of the Windows Packet Monitor (Pktmon) API. Designed for real-time network traffic monitoring with multi-threaded processing.

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
- C++20 support