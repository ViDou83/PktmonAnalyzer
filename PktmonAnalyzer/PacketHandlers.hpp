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
    // Producer handler - pushes packets to ring buffer for consumer threads
    class RingBufferHandler : public IPacketHandler {
    public:
        explicit RingBufferHandler(
            std::shared_ptr<const CaptureOptions> options,
            std::shared_ptr<const DataSourceCache> dataSourceCache)
            : IPacketHandler(std::move(options), std::move(dataSourceCache))
        {}

        void onPacketReceived(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) override {
            if (m_captureOptions->droppedOnly) {
                auto* meta = reinterpret_cast<const PACKETMONITOR_STREAM_METADATA*>(
                    static_cast<const std::byte*>(data.Data) + data.MetadataOffset);
                if (meta->DropReason == 0) {
                    return;
                }
            }

            // Take ownership of packet data (copy into owned buffer)
            PacketData packetData(data, m_captureOptions, m_dataSourceCache);

			m_packetCount.fetch_add(1, std::memory_order_relaxed);
			m_totalBytes.fetch_add(data.PacketLength, std::memory_order_relaxed);
			m_missedWrites.fetch_add(data.MissedPacketWriteCount, std::memory_order_relaxed);
			m_missedReads.fetch_add(data.MissedPacketReadCount, std::memory_order_relaxed);

            auto metadata = packetData.getMetadata();
            get_or_create_counter(m_componentStats, metadata.ComponentId).fetch_add(1, std::memory_order_relaxed);

            if (metadata.DropReason != 0) {
				m_droppedPackets.fetch_add(1, std::memory_order_relaxed);
                get_or_create_counter(m_dropReasons, metadata.DropReason).fetch_add(1, std::memory_order_relaxed);
            }

            get_or_create_counter(m_processorStats, metadata.Processor).fetch_add(1, std::memory_order_relaxed);

            // Non-blocking push to ring buffer
            if (!m_ringBuffer->tryPush(std::move(packetData))) {
                //std::cout << "Warning: Ring buffer full, dropping packet\n";
                ++m_ringBufferDrops;
            }
        }

        uint64_t getDroppedPackets() const noexcept {
            return m_ringBufferDrops.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<uint64_t> m_ringBufferDrops{ 0 };
    };

} // namespace Pktmon