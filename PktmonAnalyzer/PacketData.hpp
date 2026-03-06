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

namespace Pktmon {

class PacketData {

public:
    PacketData() = default;

    explicit PacketData(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data,
        std::shared_ptr<CaptureOptions> options,
        std::shared_ptr<DataSourceCache> dataSourceCache)
        : m_captureOptions(options),
		m_dataSourceCache(dataSourceCache)
    {
        m_metadata = std::span<const std::byte>(
            static_cast<const std::byte*>(data.Data) + data.MetadataOffset,
			sizeof(PACKETMONITOR_STREAM_METADATA));

        m_packet = std::span<const std::byte>(
            static_cast<const std::byte*>(data.Data) + data.PacketOffset,
			data.PacketLength);

        m_packetLength = data.PacketLength;
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
		auto metadata = getMetadata();
        oss << "======================================================\n";
        if (m_captureOptions->showDetailedMetadata) {
            oss << "Timestamp:        " << formatTimestamp(metadata.TimeStamp) << "\n";
            oss << "Packet Group ID:  " << std::dec << metadata.PktGroupId << "\n";
            oss << "Packet Count:     " << std::dec << metadata.PktCount << "\n";
            oss << "Appearance Count: " << std::dec << metadata.AppearanceCount << "\n";
            oss << "Direction:        " << std::dec << metadata.DirectionName << "\n";
            oss << "Packet Type:      " << std::dec << metadata.PacketType << "\n";
            if (m_dataSourceCache->size()) {
                std::wstring componentName = m_dataSourceCache->getComponentName(metadata.ComponentId);
                std::string componentStr;
                if (!componentName.empty()) {
                    int size = WideCharToMultiByte(CP_UTF8, 0, componentName.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (size > 0) {
                        componentStr.resize(size - 1);
                        WideCharToMultiByte(CP_UTF8, 0, componentName.c_str(), -1, &componentStr[0], size, nullptr, nullptr);
                    }
                }
                oss << "Component:        " << std::left <<
                    (componentStr + " (ID:" + std::to_string(metadata.ComponentId) + ")") << "\n";

            }
            else {
                oss << "Component: " << std::setw(31) << metadata.ComponentId << "\n";
            }
            oss << "Edge ID:          " << std::dec << metadata.EdgeId << "\n";
            oss << "Filter ID:        " << std::dec << metadata.FilterId << "\n";
            oss << "Processor:        " << std::dec << metadata.Processor << "\n";
            if (metadata.DropReason != 0) {
                oss << "!!!! Drop Reason   : " << static_cast<PKTMON_DROP_REASON>(metadata.DropReason) << "\n";
                oss << "!!!! Drop Location : " << static_cast<PKTMON_DROP_LOCATION>(metadata.DropLocation) << "\n";
            }
        }
        else {
            oss << "Timestamp: " << std::left << std::setw(30) << formatTimestamp(metadata.TimeStamp) << "\n";
            oss << "Packet:    " << std::setw(8) << std::dec << metadata.PktGroupId << "\n";
            if (m_dataSourceCache->size()) {
                std::wstring componentName = m_dataSourceCache->getComponentName(metadata.ComponentId);
                std::string componentStr(componentName.begin(), componentName.end());
                oss << "Component: " << std::left <<
                    (componentStr + " (ID:" + std::to_string(metadata.ComponentId) + ")") << "\n";

            }
            else {
                oss << "Component: " << std::setw(31) << metadata.ComponentId << "\n";
            }
            oss << "Processor: " << std::dec << metadata.Processor << "\n";
        }

        if (metadata.DropReason != 0) {
            oss << "!!!! Drop Reason   : " << static_cast<PKTMON_DROP_REASON>(metadata.DropReason) << "\n";
            oss << "!!!! Drop Location : " << static_cast<PKTMON_DROP_LOCATION>(metadata.DropLocation) << "\n";
        }

        oss << "======================================================" << std::endl;

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
                oss << std::endl;
            }
        }
        oss << std::endl;
    }


private:

    std::span<const std::byte> m_metadata{};
    std::span<const std::byte> m_packet{};

    uint32_t m_packetLength{};
    uint32_t m_missedWrite{};
    uint32_t m_missedRead{};
	std::shared_ptr<CaptureOptions> m_captureOptions;
	std::shared_ptr<DataSourceCache> m_dataSourceCache;

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