#pragma once

#include <windows.h>       // Add this - needed for FILETIME, SYSTEMTIME, etc.
#include <basetsd.h>
#include <string>          // Add this - for std::string, std::wstring
#include <sstream>         // Add this - for std::ostringstream  
#include <iomanip>         // Add this - for std::setfill, std::setw
#include <mutex>           // Add this - for std::mutex
#include <unordered_map>   // Add this - for std::unordered_map
#include "Pktmonapi.hpp"   // Add this - for PACKETMONITOR_DATA_SOURCE_KIND, etc.

// Helper function to format timestamp
inline std::string formatTimestamp(const LARGE_INTEGER& timestamp) {
    // Convert Windows FILETIME (100-nanosecond intervals since 1601-01-01)
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(timestamp.QuadPart & 0xFFFFFFFF);
    ft.dwHighDateTime = static_cast<DWORD>(timestamp.QuadPart >> 32);

    SYSTEMTIME st;
    if (FileTimeToSystemTime(&ft, &st)) {
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(4) << st.wYear << "-"
            << std::setw(2) << st.wMonth << "-"
            << std::setw(2) << st.wDay << " "
            << std::setw(2) << st.wHour << ":"
            << std::setw(2) << st.wMinute << ":"
            << std::setw(2) << st.wSecond << "."
            << std::setw(3) << st.wMilliseconds;
        return oss.str();
    }
    return "Invalid timestamp";
}

inline std::string wstringToString(const std::wstring& wstr) {
	std::string componentStr = "UNKNOWN";
    if (!wstr.empty()) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            componentStr.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &componentStr[0], size, nullptr, nullptr);
        }
    }
	return componentStr;
}

// Capture options structure passed from main application to packet handlers for customized behavior
struct CaptureOptions {
    int durationSeconds = 10;
    UINT16 truncationSize = 0;
    UINT16 displayLength = 0; // Number of bytes to display in hex dump (0 = no limit)
    bool showDetailedMetadata = false;
    bool droppedOnly = false;
    bool useMultiThreaded = true;
    size_t numConsumerThreads = 4; // Default to 4 consumer threads in multi-threaded mode
    size_t ringBufferSize = 2 * 1024;
};

// Data source cache for efficient lookup
class DataSourceCache {
public:
    struct DataSourceInfo {
        std::wstring name{};
        std::wstring description{};
        PACKETMONITOR_DATA_SOURCE_KIND kind{ PacketMonitorDataSourceKindAll };
        UINT32 parentId{ 0 };
    };

    void add(const PACKETMONITOR_DATA_SOURCE_SPECIFICATION& spec) {
        std::lock_guard<std::mutex> lock(m_mutex);

        DataSourceInfo info;
        info.name = spec.Name;
        info.description = spec.Description;
        info.kind = spec.Kind;
        info.parentId = spec.ParentId;

        m_componentMap[spec.Id] = info;

        // Also cache by secondary ID if present
        if (spec.SecondaryId != 0) {
            m_componentMap[spec.SecondaryId] = info;
        }
    }

    bool lookup(UINT32 componentId, DataSourceInfo& outInfo) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_componentMap.find(componentId);
        if (it != m_componentMap.end()) {
            outInfo = it->second;
            return true;
        }
        return false;
    }

    std::string getComponentName(UINT32 componentId) const {
        DataSourceInfo info;
        if (lookup(componentId, info)) {
			return wstringToString(info.name);
        }
        return "Unknown Component";
    }

    std::string getComponentDescription(UINT32 componentId) const {
        DataSourceInfo info;
        if (lookup(componentId, info)) {
            return wstringToString(info.description);
        }
        return "Unknown Description";
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_componentMap.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_componentMap.size();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<UINT32, DataSourceInfo> m_componentMap;
};