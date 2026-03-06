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
#include <concurrent_unordered_map.h>  // PPL

namespace Pktmon {

    // Producer handler - pushes packets to ring buffer for consumer threads
    class RingBufferHandler : public IPacketHandler {
    public:
        explicit RingBufferHandler(
            std::shared_ptr<RingBuffer<PacketData>> ringBuffer,
            std::shared_ptr<CaptureOptions> options,
            std::shared_ptr<DataSourceCache> dataSourceCache)
            : IPacketHandler(options)
            , m_ringBuffer(std::move(ringBuffer))
            , m_dataSourceCache(std::move(dataSourceCache)) {
        }

        void onPacketReceived(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) override {
            // Early filter for dropped-only mode
            if (m_captureOptions->droppedOnly) {
                auto* meta = reinterpret_cast<const PACKETMONITOR_STREAM_METADATA*>(
                    static_cast<const std::byte*>(data.Data) + data.MetadataOffset);
                if (meta->DropReason == 0) {
                    return;
                }
            }

            // Take ownership of packet data (copy into owned buffer)
            PacketData packetData(data, m_captureOptions, m_dataSourceCache);

            // Non-blocking push to ring buffer
            if (!m_ringBuffer->tryPush(std::move(packetData))) {
                //std::cout << "Warning: Ring buffer full, dropping packet\n";
                ++m_droppedPackets;
            }
        }

        uint64_t getDroppedPackets() const noexcept {
            return m_droppedPackets.load(std::memory_order_relaxed);
        }

    private:
        std::shared_ptr<RingBuffer<PacketData>> m_ringBuffer;
        std::shared_ptr<DataSourceCache> m_dataSourceCache;
        std::atomic<uint64_t> m_droppedPackets{0};
    };

    // Enhanced hex dump handler with metadata
    class HexDumpHandler : public IPacketHandler {
    public:
        explicit HexDumpHandler(std::shared_ptr<CaptureOptions> options) : IPacketHandler(options) {}
        
        void onPacketReceived(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) override {
            // I want to display data.Data  pointer + data.PacketOffset
            std::cout << "Memory Address: " << static_cast<const void*>(static_cast<const std::byte*>(data.Data) + data.PacketOffset) << ", Length: " << std::dec << data.PacketLength << " bytes\n";

            auto& api = Pktmon::ApiManager::getInstance();

            PacketData packetData(data, m_captureOptions, api.getDataSourceCache());

            if (!m_captureOptions->droppedOnly || 
				m_captureOptions->droppedOnly && packetData.isDropped()
                ){
				std::ostringstream oss;
				packetData.printMetadata(oss);
                packetData.printPacketData(oss);
				std::cout << oss.str();
            }
        }
    };

    // Statistics handler with metadata analysis
    class StatisticsHandler : public IPacketHandler {
    public:

		StatisticsHandler() = default;

        static std::atomic<uint64_t>& get_or_create_counter(
            concurrency::concurrent_unordered_map<uint32_t, std::shared_ptr<std::atomic<uint64_t>>>& m,
            uint32_t key)
        {
            // insert a ptr if missing; safe because shared_ptr is copy/move
            auto [it, inserted] = m.insert({ key, std::make_shared<std::atomic<uint64_t>>(0) });
            return *(it->second);
        }

        void onPacketReceived(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) override {

			// print data memory address and length for 
            ++m_packetCount;
            m_totalBytes += data.PacketLength;
            m_missedWrites += data.MissedPacketWriteCount;
            m_missedReads += data.MissedPacketReadCount;

            auto& api = Pktmon::ApiManager::getInstance();

            auto packetData = std::make_shared<Pktmon::PacketData>(data, m_captureOptions, api.getDataSourceCache());
			auto metadata = packetData->getMetadata();
			get_or_create_counter(m_componentStats, metadata.ComponentId).fetch_add(1, std::memory_order_relaxed);

            if (metadata.DropReason != 0) {
                ++m_droppedPackets;
				get_or_create_counter(m_dropReasons, metadata.DropReason).fetch_add(1, std::memory_order_relaxed);
            }

			get_or_create_counter(m_processorStats, metadata.Processor).fetch_add(1, std::memory_order_relaxed);
        }

        void printStatistics() const {
			constexpr auto separator = "==========================\n";

            std::cout << separator;

            std::cout << "\n=== Capture Statistics ===\n";
            std::cout << "Total Packets: " << m_packetCount << "\n";
            std::cout << "Total Bytes: " << m_totalBytes << "\n";
            std::cout << "Dropped Packets: " << m_droppedPackets << "\n";
            std::cout << "Missed Writes: " << m_missedWrites << "\n";
            std::cout << "Missed Reads: " << m_missedReads << "\n";

            if (!m_componentStats.empty()) {
                std::cout << "\n--- Per-Component Statistics ---\n";
                for (const auto& [componentId, count] : m_componentStats) {
                    std::cout << "  Component " << componentId << ": " << count->load() << " packets\n";
                }
            }

            if (!m_processorStats.empty()) {
                std::cout << "\n--- Per-Processor Statistics ---\n";
                for (const auto& [processor, count] : m_processorStats) {
                    std::cout << "  Processor " << processor << ": " << count->load() << " packets\n";
                }
            }

            if (!m_dropReasons.empty()) {
                std::cout << "\n--- Drop Reasons ---\n";
                for (const auto& [reason, count] : m_dropReasons) {
                    std::cout << "  Reason 0x" << std::hex << reason << " " << "count:" << std::dec << count->load() << "\n";
                }
            }

            std::cout << separator;
        }

    private:
        std::atomic<uint64_t> m_packetCount{0};
        std::atomic<uint64_t> m_totalBytes{0};
        std::atomic<uint64_t> m_droppedPackets{0};
        std::atomic<uint64_t> m_missedWrites{0};
        std::atomic<uint64_t> m_missedReads{0};

        //std::map<UINT16, uint64_t> m_componentStats;
        //std::map<UINT16, uint64_t> m_processorStats;
        //std::map<UINT32, uint64_t> m_dropReasons;
		concurrency::concurrent_unordered_map<uint32_t, std::shared_ptr<std::atomic<uint64_t>>> m_componentStats;
		concurrency::concurrent_unordered_map<uint32_t, std::shared_ptr<std::atomic<uint64_t>>> m_processorStats;
		concurrency::concurrent_unordered_map<uint32_t, std::shared_ptr<std::atomic<uint64_t>>> m_dropReasons;
    };

} // namespace Pktmon