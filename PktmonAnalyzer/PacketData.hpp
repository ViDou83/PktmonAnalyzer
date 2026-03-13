#pragma once

#include "Pktmonapi.hpp"
#include "PktMonLoc.hpp"
#include "PktmonUtils.h"

#include <windows.h>
#include <cstddef>
#include <vector>
#include <memory>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <format>
#include <sstream>
#include <array>

namespace Pktmon {

class PacketData {

public:
    PacketData() = default;

    explicit PacketData(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data,
        std::shared_ptr<const CaptureOptions> options,
        std::shared_ptr<const DataSourceCache> dataSourceCache) noexcept
        : m_captureOptions(std::move(options)),
		m_dataSourceCache(std::move(dataSourceCache))
    {
        m_packetLength = (data.DataSize <= 9000) ? data.DataSize : 9000;

        std::memcpy(m_packet.data(),
            static_cast<const std::byte*>(data.Data) + data.PacketOffset,
            m_packetLength);

        std::memcpy(m_metadata.data(),
            static_cast<const std::byte*>(data.Data) + data.MetadataOffset,
            m_metadata.size());

        m_missedWrite = data.MissedPacketWriteCount;
        m_missedRead = data.MissedPacketReadCount;
    }

    // move-only
    PacketData(PacketData&&) noexcept = default;
    PacketData& operator=(PacketData&&) noexcept = default;
    PacketData(const PacketData&) = delete;
    PacketData& operator=(const PacketData&) = delete;

    PACKETMONITOR_STREAM_METADATA getMetadata() const {
        return *reinterpret_cast<const PACKETMONITOR_STREAM_METADATA*>(m_metadata.data());
	}
    bool isDropped() const {
        return getMetadata().DropReason != 0;
	}
    
    void printMetadata(std::ostringstream& oss){
        const auto metadata = getMetadata();
        constexpr std::string_view separator = "======================================================\n";

        oss << separator;

        // Common: Timestamp always shown
        oss << std::format("{:<18}{}\n", "Timestamp:", formatTimestamp(metadata.TimeStamp));
        oss << std::format("{:<18}{}\n", "Processor:", metadata.Processor);
        oss << std::format("{:<18}{}\n", "Packet Group ID:", metadata.PktGroupId);

        const auto packetTypeStr = pktmonPacketTypeToString(static_cast<PKTMON_PACKET_TYPE>(metadata.PacketType));
        const auto directionStr = pktmonDirectionTagToString(static_cast<PKTMON_DIRECTION_TAG>(metadata.DirectionName));

        oss << std::format("{:<18}{}\n", "Packet Type:", packetTypeStr);
		oss << std::format("{:<18}{}\n", "Packet Direction:", directionStr);

        if (m_captureOptions->showDetailedMetadata) {
            oss << std::format("{:<18}{}\n", "Packet Count:", metadata.PktCount);
            oss << std::format("{:<18}{}\n", "Appearance Count:", metadata.AppearanceCount);
        }

        if (m_dataSourceCache->size()) {
            std::string componentName = m_dataSourceCache->getComponentName(metadata.ComponentId);
            std::string EdgeName = m_dataSourceCache->getComponentName(metadata.EdgeId);
            oss << std::format("{:<18}{} (ID:{})\n", "Component:", componentName, metadata.ComponentId);
            oss << std::format("{:<18}{} (ID:{})\n", "Edge:", EdgeName, metadata.EdgeId);
        } else {
            oss << std::format("{:<18}{}\n", "Component:", metadata.ComponentId);
            oss << std::format("{:<18}{}\n", "Edge:", metadata.EdgeId);
        }

        // Drop info (if applicable)
        if (metadata.DropReason != 0) {
            const auto dropReasonStr = pktmonDropReasonToString(static_cast<PKTMON_DROP_REASON>(metadata.DropReason));
            const auto dropLocationStr = pktmonDropLocationToString(static_cast<PKTMON_DROP_LOCATION>(metadata.DropLocation));
            oss << std::format("{:<18}{} (0x{:X})\n", "Drop Reason:", dropReasonStr, metadata.DropReason);
            oss << std::format("{:<18}{} (0x{:X})\n", "Drop Location:", dropLocationStr, metadata.DropLocation);
        }

        oss << separator;
    }

    void printPacketData(std::ostringstream& oss) {
        oss << "Packet: " << m_packetLength << " bytes\n";

		auto displayLength = m_captureOptions->displayLength == 0 ? m_packetLength : std::min<size_t>(static_cast<size_t>(m_captureOptions->displayLength), m_packetLength);

        for (size_t i = 0; i < displayLength; ++i) {
            if (i % 16 == 0) {
                oss << std::hex << std::uppercase << std::right << std::setfill('0') << std::setw(4) << i << ": ";
            }
            oss << std::hex << std::uppercase << std::right << std::setfill('0') << std::setw(2) << static_cast<int>(m_packet[i]) << " ";
            if ((i + 1) % 16 == 0) {
                oss << "\n";
            }
        }
        oss << "\n";
    }


private:
    std::array<std::byte, sizeof(PACKETMONITOR_STREAM_METADATA)> m_metadata;
    std::array<std::byte, 9000> m_packet;

    uint32_t m_packetLength{};
    uint32_t m_missedWrite{};
    uint32_t m_missedRead{};
    std::shared_ptr<const CaptureOptions> m_captureOptions;
    std::shared_ptr<const DataSourceCache> m_dataSourceCache;

    static const PACKETMONITOR_STREAM_METADATA* extractMetadata(
        const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) {
        
        if (data.MetadataOffset + sizeof(PACKETMONITOR_STREAM_METADATA) > data.DataSize) {
            return nullptr;
        }
        
        auto buffer = static_cast<const BYTE*>(data.Data);
        return reinterpret_cast<const PACKETMONITOR_STREAM_METADATA*>(
            buffer + data.MetadataOffset);
    }
};

} // namespace Pktmon