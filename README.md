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

## Usage
Run the binary w/o aguments to start a real time packet capture with default settings. 

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
      PktmonAnalyzer.exe -enum
      
      PktmonAnalyzer.exe -capture 30
      
      PktmonAnalyzer.exe -capture 60 -threads 4
      
      PktmonAnalyzer.exe -capture 60 -threads 4 -bufsize 50000
  
      PktmonAnalyzer.exe -capture 60 -drop -detailed -threads 2
  
