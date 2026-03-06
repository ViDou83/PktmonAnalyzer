#pragma once

#include "PktmonApiWrapper.hpp"
#include "RingBuffer.hpp"
#include "PacketData.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <map>

namespace Pktmon {

    // Ring buffer packet handler (producer - callback thread)
    //class RingBufferHandler : public IPacketHandler {
    //public:
    //    explicit RingBufferHandler(
    //        std::shared_ptr<RingBuffer<PacketData>> ringBuffer,
    //        CaptureOptions options = {},
    //        std::shared_ptr<DataSourceCache> dataSourceCache = nullptr)
    //        : IPacketHandler(options, dataSourceCache)
    //        , m_ringBuffer(std::move(ringBuffer)) {
    //    }

    //    void onPacketReceived(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) override {
    //        if (m_captureOptions.droppedOnly) {
    //            auto metadata = extractMetadata(data);
    //            if (!metadata || metadata->DropReason == 0) {
    //                return;
    //            }
    //        }
    //        
    //        PacketData packetData = PacketData::fromDescriptor(data);
    //        
    //        if (!m_ringBuffer->tryPush(std::move(packetData))) {
    //            ++m_droppedPackets;
    //        }
    //    }

    //    uint64_t getDroppedPackets() const {
    //        return m_droppedPackets.load();
    //    }

    //private:
    //    std::shared_ptr<RingBuffer<PacketData>> m_ringBuffer;
    //    std::atomic<uint64_t> m_droppedPackets{0};
    //};

    //// Multi-threaded packet processor
    //class MultiThreadedPacketProcessor {
    //public:
    //    explicit MultiThreadedPacketProcessor(
    //        std::shared_ptr<RingBuffer<PacketData>> ringBuffer,
    //        size_t numThreads,
    //        CaptureOptions options = {},
    //        std::shared_ptr<DataSourceCache> dataSourceCache = nullptr)
    //        : m_ringBuffer(std::move(ringBuffer))
    //        , m_numThreads(numThreads)
    //        , m_captureOptions(options)
    //        , m_dataSourceCache(dataSourceCache)
    //        , m_running(false) {
    //    }

    //    ~MultiThreadedPacketProcessor() {
    //        stop();
    //    }

    //    // Start consumer threads
    //    void start() {
    //        if (m_running.load()) {
    //            return;
    //        }
    //        
    //        m_running.store(true);
    //        
    //        // Create worker threads
    //        for (size_t i = 0; i < m_numThreads; ++i) {
    //            m_threads.emplace_back(&MultiThreadedPacketProcessor::workerThread, this, i);
    //        }
    //        
    //        std::cout << "Started " << m_numThreads << " consumer threads\n";
    //    }

    //    // Stop all consumer threads
    //    void stop() {
    //        if (!m_running.load()) {
    //            return;
    //        }
    //        
    //        m_running.store(false);
    //        m_ringBuffer->stop();
    //        
    //        // Join all threads
    //        for (auto& thread : m_threads) {
    //            if (thread.joinable()) {
    //                thread.join();
    //            }
    //        }
    //        
    //        m_threads.clear();
    //        std::cout << "\nAll consumer threads stopped\n";
    //    }

    //    // Get statistics
    //    struct ThreadStatistics {
    //        size_t threadId;
    //        uint64_t packetsProcessed;
    //    };

    //    std::vector<ThreadStatistics> getStatistics() const {
    //        std::vector<ThreadStatistics> stats;
    //        std::lock_guard<std::mutex> lock(m_statsMutex);
    //        for (size_t i = 0; i < m_threadStats.size(); ++i) {
    //            stats.push_back({i, m_threadStats[i].load()});
    //        }
    //        return stats;
    //    }

    //    uint64_t getTotalProcessed() const {
    //        uint64_t total = 0;
    //        std::lock_guard<std::mutex> lock(m_statsMutex);
    //        for (const auto& stat : m_threadStats) {
    //            total += stat.load();
    //        }
    //        return total;
    //    }

    //private:
    //    void workerThread(size_t threadId) {
    //        {
    //            std::lock_guard<std::mutex> lock(m_statsMutex);
    //            if (threadId >= m_threadStats.size()) {
    //                m_threadStats.resize(threadId + 1);
    //            }
    //        }

    //        while (m_running.load()) {
    //            // Pop packet from ring buffer (blocking)
    //            auto packetOpt = m_ringBuffer->pop();
    //            
    //            if (!packetOpt.has_value()) {
    //                break; // Ring buffer stopped
    //            }
    //            
    //            auto& packet = packetOpt.value();
    //            processPacket(packet, threadId);
    //            
    //            m_threadStats[threadId]++;
    //        }
    //    }

    //    void processPacket(const PacketData& packet, size_t threadId) {
    //        std::lock_guard<std::mutex> lock(m_outputMutex);
    //        
    //        std::cout << "[Thread " << threadId << "] ";
    //        std::cout << "Packet #" << packet.metadata.PktGroupId 
    //                  << " Component:" << packet.metadata.ComponentId
    //                  << " Length:" << packet.packet.size() << " bytes\n";
    //    }

    //    std::shared_ptr<RingBuffer<PacketData>> m_ringBuffer;
    //    size_t m_numThreads;
    //    CaptureOptions m_captureOptions;
    //    std::shared_ptr<DataSourceCache> m_dataSourceCache;
    //    
    //    std::atomic<bool> m_running;
    //    std::vector<std::thread> m_threads;
    //    std::vector<std::atomic<uint64_t>> m_threadStats;
    //    
    //    mutable std::mutex m_statsMutex;
    //    std::mutex m_outputMutex;
    //};

    // Enhanced hex dump handler with metadata
    class HexDumpHandler : public IPacketHandler {
    public:
        HexDumpHandler() : IPacketHandler() {}

        explicit HexDumpHandler(std::shared_ptr<CaptureOptions> options,
                                std::shared_ptr<DataSourceCache> dataSourceCache)
            : IPacketHandler(options, dataSourceCache) {}

        void onPacketReceived(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) override {
            // I want to display data.Data  pointer + data.PacketOffset
            std::cout << "Memory Address: " << static_cast<const void*>(static_cast<const std::byte*>(data.Data) + data.PacketOffset) << ", Length: " << std::dec << data.PacketLength << " bytes\n";

            PacketData packetData(data, m_captureOptions, m_dataSourceCache);

            if (!m_captureOptions->droppedOnly || 
				m_captureOptions->droppedOnly && packetData.isDropped()
                ){
				packetData.printMetadata();
                packetData.printPacketData();
            }
        }
    };

    // Statistics handler with metadata analysis
    class StatisticsHandler : public IPacketHandler {
    public:
        StatisticsHandler() : IPacketHandler() {}
        
        void onPacketReceived(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) override {

			// print data memory address and length for 
            ++m_packetCount;
            m_totalBytes += data.PacketLength;
            m_missedWrites += data.MissedPacketWriteCount;
            m_missedReads += data.MissedPacketReadCount;

            auto packetData = std::make_shared<Pktmon::PacketData>(data, m_captureOptions, m_dataSourceCache);
			auto metadata = packetData->getMetadata();
            if (packetData) {
                m_componentStats[metadata.ComponentId]++;

                if (metadata.DropReason != 0) {
                    ++m_droppedPackets;
                    m_dropReasons[metadata.DropReason]++;
                }

                m_processorStats[metadata.Processor]++;
            }
        }

        void printStatistics() const {
            std::cout << "\n=== Capture Statistics ===\n";
            std::cout << "Total Packets: " << m_packetCount << "\n";
            std::cout << "Total Bytes: " << m_totalBytes << "\n";
            std::cout << "Dropped Packets: " << m_droppedPackets << "\n";
            std::cout << "Missed Writes: " << m_missedWrites << "\n";
            std::cout << "Missed Reads: " << m_missedReads << "\n";

            if (!m_componentStats.empty()) {
                std::cout << "\n--- Per-Component Statistics ---\n";
                for (const auto& [componentId, count] : m_componentStats) {
                    std::cout << "  Component " << componentId << ": " << count << " packets\n";
                }
            }

            if (!m_processorStats.empty()) {
                std::cout << "\n--- Per-Processor Statistics ---\n";
                for (const auto& [processor, count] : m_processorStats) {
                    std::cout << "  Processor " << processor << ": " << count << " packets\n";
                }
            }

            if (!m_dropReasons.empty()) {
                std::cout << "\n--- Drop Reasons ---\n";
                for (const auto& [reason, count] : m_dropReasons) {
                    std::cout << "  Reason 0x" << std::hex << reason << std::dec
                        << ": " << count << " packets\n";
                }
            }

            std::cout << "==========================\n";
        }

    private:
        std::atomic<uint64_t> m_packetCount{0};
        std::atomic<uint64_t> m_totalBytes{0};
        std::atomic<uint64_t> m_droppedPackets{0};
        std::atomic<uint64_t> m_missedWrites{0};
        std::atomic<uint64_t> m_missedReads{0};

        std::map<UINT16, uint64_t> m_componentStats;
        std::map<UINT16, uint64_t> m_processorStats;
        std::map<UINT32, uint64_t> m_dropReasons;
    };

} // namespace Pktmon