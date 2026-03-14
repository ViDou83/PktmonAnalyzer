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
        m_packetLength = (data.DataSize <= m_captureOptions->truncationSize) ? data.DataSize : m_captureOptions->truncationSize;
        
		m_packet.reserve(m_packetLength);

        std::copy(static_cast<const std::byte*>(data.Data) + data.PacketOffset,
            static_cast<const std::byte*>(data.Data) + data.PacketOffset + m_packetLength,
            std::back_inserter(m_packet));

        //std::memcpy(static_cast<void*>(&m_packet),
        //    static_cast<const std::byte*>(data.Data) + data.PacketOffset,
        //    m_packetLength);

        std::memcpy(static_cast<void*>(&m_metadata),
            static_cast<const std::byte*>(data.Data) + data.MetadataOffset,
            sizeof(PACKETMONITOR_STREAM_METADATA));


        m_missedWrite = data.MissedPacketWriteCount;
        m_missedRead = data.MissedPacketReadCount;
    }

    // move-only
    PacketData(PacketData&&) noexcept = default;
    PacketData& operator=(PacketData&&) noexcept = default;
    PacketData(const PacketData&) = delete;
    PacketData& operator=(const PacketData&) = delete;

    PACKETMONITOR_STREAM_METADATA getMetadata() const {
        return m_metadata;
	}
    bool isDropped() const {
        return getMetadata().DropReason != 0;
	}
    
    void printMetadata(std::string& oss){
        const auto metadata = getMetadata();
        constexpr std::string_view separator = "======================================================\n";

        oss.append(separator);

        // Common: Timestamp always shown
        formatTimestampInto(oss, metadata.TimeStamp);
        std::format_to(std::back_inserter(oss),
            "{:<18}{}\n{:<18}{}\n",
            "Processor:", metadata.Processor,
            "Packet Group ID:", metadata.PktGroupId);

        const auto packetTypeStr = pktmonPacketTypeToString(static_cast<PKTMON_PACKET_TYPE>(metadata.PacketType));
        const auto directionStr = pktmonDirectionTagToString(static_cast<PKTMON_DIRECTION_TAG>(metadata.DirectionName));

        std::format_to(std::back_inserter(oss),
            "{:<18}{}\n{:<18}{}\n",
            "Packet Type:", packetTypeStr,
            "Packet Direction:", directionStr);

        if (m_captureOptions->showDetailedMetadata) {
            std::format_to(std::back_inserter(oss),
                "{:<18}{}\n{:<18}{}\n",
                "Packet Count:", metadata.PktCount,
                "Appearance Count:", metadata.AppearanceCount);
        }

        if (m_dataSourceCache->hasSources()) {
            std::string componentName = m_dataSourceCache->getComponentName(metadata.ComponentId);
            std::string EdgeName = m_dataSourceCache->getComponentName(metadata.EdgeId);
            std::format_to(std::back_inserter(oss),
                "{:<18}{} (ID:{})\n{:<18}{} (ID:{})\n",
                "Component:", componentName, metadata.ComponentId,
                "Edge:", EdgeName, metadata.EdgeId);
        } else {
            std::format_to(std::back_inserter(oss),
                "{:<18}{}\n{:<18}{}\n",
                "Component:", metadata.ComponentId,
                "Edge:", metadata.EdgeId);
        }

        // Drop info (if applicable)
        if (metadata.DropReason != 0) {
            const auto dropReasonStr = pktmonDropReasonToString(static_cast<PKTMON_DROP_REASON>(metadata.DropReason));
            const auto dropLocationStr = pktmonDropLocationToString(static_cast<PKTMON_DROP_LOCATION>(metadata.DropLocation));
            std::format_to(std::back_inserter(oss),
                "{:<18}{} (0x{:X})\n{:<18}{} (0x{:X})\n",
                "Drop Reason:", dropReasonStr, metadata.DropReason,
                "Drop Location:", dropLocationStr, metadata.DropLocation);
        }

        oss.append(separator);
    }

    void printPacketData(std::string& oss) {
        oss.append("Packet: " + m_packetLength);
		oss.append(" bytes\n");

        auto displayLength = m_captureOptions->displayLength == 0 ? m_packetLength : std::min<size_t>(static_cast<size_t>(m_captureOptions->displayLength), m_packetLength);

        for (size_t i = 0; i < displayLength; ++i) {
            if (i % 16 == 0) {
                std::format_to(std::back_inserter(oss),
                    "{:04X}: ",
					static_cast<unsigned>(i));
            }
            std::format_to(std::back_inserter(oss),
                    "{:02X} ",
				    static_cast<unsigned char>(m_packet[i]));
            if ((i + 1) % 16 == 0) {
                oss.append("\n");
            }
        }
        oss.append("\n");
    }


private:
    std::vector<std::byte> m_packet;
    PACKETMONITOR_STREAM_METADATA m_metadata{};
    std::shared_ptr<const CaptureOptions> m_captureOptions;
    std::shared_ptr<const DataSourceCache> m_dataSourceCache;

    uint32_t m_packetLength{};
    uint32_t m_missedWrite{};
    uint32_t m_missedRead{};

    // Write timestamp directly into stream without intermediate string allocation
    static void formatTimestampInto(std::string& oss, const LARGE_INTEGER& timestamp) {
        FILETIME ft;
        ft.dwLowDateTime = static_cast<DWORD>(timestamp.QuadPart & 0xFFFFFFFF);
        ft.dwHighDateTime = static_cast<DWORD>(timestamp.QuadPart >> 32);

        SYSTEMTIME st;
        if (FileTimeToSystemTime(&ft, &st)) {
            std::format_to(std::back_inserter(oss),
                "{:<18}{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}\n",
                "Timestamp:",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        } else {
            oss.append("Timestamp:        Invalid timestamp\n");
        }
    }

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