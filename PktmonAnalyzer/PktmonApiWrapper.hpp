#pragma once

#include <windows.h>
#include <memory>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ostream>
#include <span>
#include <concurrent_unordered_map.h>  // PPL

#include "Pktmonapi.hpp"
#include "PktMonLoc.hpp"
#include "PacketData.hpp"
#include "RingBuffer.hpp"

namespace Pktmon {

    // Packet handler interface
class IPacketHandler {

public:
    IPacketHandler() = default;

    // truncate packet data to user provided bytes for hex dump - default 64 bytes
	explicit IPacketHandler(std::shared_ptr<const CaptureOptions> options, 
                        std::shared_ptr<const DataSourceCache> dataSourceCache)
        : m_captureOptions(std::move(options)), m_dataSourceCache(std::move(dataSourceCache)) {
    }
    virtual ~IPacketHandler() = default;
    virtual void onPacketReceived(const PACKETMONITOR_STREAM_DATA_DESCRIPTOR& data) = 0;
    virtual void onStreamEvent(const PACKETMONITOR_STREAM_EVENT_INFO& info,
        PACKETMONITOR_STREAM_EVENT_KIND kind) {
    }

    // get truncation size for hex dump
    UINT16 getTruncationSize() const noexcept { return m_captureOptions->truncationSize; }
    void attachRingBuffer(std::shared_ptr<RingBuffer<PacketData>> ringBuffer) { m_ringBuffer = ringBuffer; }

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

protected:
    static std::atomic<uint64_t>& get_or_create_counter(
        concurrency::concurrent_unordered_map<uint32_t, std::shared_ptr<std::atomic<uint64_t>>>& m,
        uint32_t key)
    {
        // insert a ptr if missing; safe because shared_ptr is copy/move
        auto [it, inserted] = m.insert({ key, std::make_shared<std::atomic<uint64_t>>(0) });
        return *(it->second);
    }

    std::shared_ptr<const CaptureOptions> m_captureOptions;
    std::shared_ptr<RingBuffer<PacketData>> m_ringBuffer;
    std::shared_ptr<const DataSourceCache> m_dataSourceCache;

    std::atomic<uint64_t> m_packetCount{ 0 };
    std::atomic<uint64_t> m_totalBytes{ 0 };
    std::atomic<uint64_t> m_droppedPackets{ 0 };
    std::atomic<uint64_t> m_missedWrites{ 0 };
    std::atomic<uint64_t> m_missedReads{ 0 };
    concurrency::concurrent_unordered_map<uint32_t, std::shared_ptr<std::atomic<uint64_t>>> m_componentStats;
    concurrency::concurrent_unordered_map<uint32_t, std::shared_ptr<std::atomic<uint64_t>>> m_processorStats;
    concurrency::concurrent_unordered_map<uint32_t, std::shared_ptr<std::atomic<uint64_t>>> m_dropReasons;

private:
};

// Exception classes
class PktmonException : public std::runtime_error {
public:
    explicit PktmonException(const std::string& message, HRESULT hr = E_FAIL)
        : std::runtime_error(message), m_hr(hr) {}
    
    HRESULT getHResult() const noexcept { return m_hr; }
private:
    HRESULT m_hr;
};

// Forward declarations
class Session;
class RealtimeStream;

// API Manager (Singleton)
class ApiManager {
public:
    static ApiManager& getInstance();
    
    // Delete copy/move
    ApiManager(const ApiManager&) = delete;
    ApiManager& operator=(const ApiManager&) = delete;
    ApiManager(ApiManager&&) = delete;
    ApiManager& operator=(ApiManager&&) = delete;
    
    ~ApiManager();
    
    // Initialize the API
    void initialize(UINT32 version = PACKETMONITOR_API_VERSION_1_0);
    void shutdown();
    
    bool isInitialized() const noexcept { return m_initialized.load(); }
    HANDLE getHandle() const noexcept { return m_handle; }
    
    // Session management
    std::shared_ptr<Session> createSession(const std::wstring& name);
    
    // Data source enumeration
    std::vector<PACKETMONITOR_DATA_SOURCE_SPECIFICATION> enumerateDataSources(
        PACKETMONITOR_DATA_SOURCE_KIND kind = PacketMonitorDataSourceKindAll,
        bool includeHidden = true);

    // Get the data source cache
    std::shared_ptr<DataSourceCache> getDataSourceCache() { return m_dataSourceCache; }

    // Refresh the data source cache
    void refreshDataSourceCache();

private:
    ApiManager();
    
    void loadLibrary();
    void resolveFunctions();
    
    HMODULE m_hModule{nullptr};
    HANDLE m_handle{nullptr};
    std::atomic<bool> m_initialized{false};
    std::mutex m_mutex;
    std::shared_ptr<DataSourceCache> m_dataSourceCache;

    // Function pointers
    using PacketMonitorAddCaptureConstraint = HRESULT(WINAPI*)(HANDLE, PACKETMONITOR_PROTOCOL_CONSTRAINT *);
	using PacketMonitorAddSingleDataSourceToSession = HRESULT(WINAPI*)(HANDLE, const PACKETMONITOR_DATA_SOURCE_SPECIFICATION*);
	using PacketMonitorAttachOutputToSession = HRESULT(WINAPI*)(HANDLE, void*);
	using PacketMonitorCloseRealtimeStream = void(WINAPI*)(HANDLE);
	using PacketMonitorCloseSessionHandle = void(WINAPI*)(HANDLE);
	using PacketMonitorCreateLiveSession = HRESULT(WINAPI*)(HANDLE, LPCWSTR, HANDLE*);
	using PacketMonitorCreateRealtimeStream = HRESULT(WINAPI*)(HANDLE, void*, void*);
	using PacketMonitorEnumDataSources = HRESULT(WINAPI*)(HANDLE, PACKETMONITOR_DATA_SOURCE_KIND,
                                                            BOOLEAN, SIZE_T, SIZE_T*,
                                                            PACKETMONITOR_DATA_SOURCE_LIST*);
	using PacketMonitorInitialize = HRESULT(WINAPI*)(UINT32, void*, HANDLE*);
	using PacketMonitorSetSessionActive = HRESULT(WINAPI*)(HANDLE, BOOLEAN);
	using PacketMonitorUninitialize = HRESULT(WINAPI*)(HANDLE);
    //HRESULT
    //    WINAPI
    //    PacketMonitorAddCaptureConstraint(
    //        _In_ PACKETMONITOR_SESSION session,
    //        _In_ PACKETMONITOR_PROTOCOL_CONSTRAINT const* captureConstraint
    //    );
    PacketMonitorAddCaptureConstraint m_pfnAddCaptureConstraint{ nullptr };
    /*HRESULT
        WINAPI
        PacketMonitorAddSingleDataSourceToSession(
            _In_ PACKETMONITOR_SESSION session,
            _In_ PACKETMONITOR_DATA_SOURCE_SPECIFICATION const* dataSource
        );*/
	PacketMonitorAddSingleDataSourceToSession m_pfnAddSingleDataSourceToSession{ nullptr };
    /*HRESULT
        WINAPI
        PacketMonitorAttachOutputToSession(
            _In_ PACKETMONITOR_SESSION session,
            _In_ VOID* outputHandle
        );*/
	PacketMonitorAttachOutputToSession m_pfnAttachOutputToSession{ nullptr };
    /*VOID
        WINAPI
        PacketMonitorCloseRealtimeStream(
            _In_ PACKETMONITOR_REALTIME_STREAM realtimeStream
        );*/
	PacketMonitorCloseRealtimeStream m_pfnCloseRealtimeStream{ nullptr };
    /*VOID
        WINAPI
        PacketMonitorCloseSessionHandle(
            _In_ PACKETMONITOR_SESSION session
        );*/
	PacketMonitorCloseSessionHandle m_pfnCloseSessionHandle{ nullptr };
    //HRESULT
    //    WINAPI
    //    PacketMonitorCreateLiveSession(
    //        _In_ PACKETMONITOR_HANDLE handle,
    //        _In_ PCWSTR name,
    //        _Out_ PACKETMONITOR_SESSION* session
    //    );
	PacketMonitorCreateLiveSession m_pfnCreateSession{ nullptr };
    /*HRESULT
        WINAPI
        PacketMonitorCreateRealtimeStream(
            _In_ PACKETMONITOR_HANDLE handle,
            _In_ PACKETMONITOR_REALTIME_STREAM_CONFIGURATION const* configuration,
            _Out_ PACKETMONITOR_REALTIME_STREAM* realtimeStream
        );*/
	PacketMonitorCreateRealtimeStream m_pfnCreateRealtimeStream{ nullptr };
    /*HRESULT
        WINAPI
        PacketMonitorEnumDataSources(
            _In_ PACKETMONITOR_HANDLE handle,
            _In_ PACKETMONITOR_DATA_SOURCE_KIND sourceKind,
            _In_ BOOLEAN showHidden,
            _In_ SIZE_T bufferCapacity,
            _Out_ SIZE_T* bytesNeeded,
            _Out_writes_bytes_opt_(bufferCapacity) PACKETMONITOR_DATA_SOURCE_LIST* dataSourceList
        );*/
	PacketMonitorEnumDataSources m_pfnEnumDataSources{ nullptr };
    /*HRESULT
        WINAPI
        PacketMonitorInitialize(
            _In_ UINT32 apiVersion,
            _Reserved_ void* reserved,
            _Out_ PACKETMONITOR_HANDLE* handle
        );*/
    PacketMonitorInitialize m_pfnInitialize{ nullptr };
    /*HRESULT
        WINAPI
        PacketMonitorSetSessionActive(
            _In_ PACKETMONITOR_SESSION session,
            _In_ BOOLEAN active
        );*/
	PacketMonitorSetSessionActive m_pfnSetSessionActive{ nullptr };
    /*VOID
        WINAPI
        PacketMonitorUninitialize(
            _In_ PACKETMONITOR_HANDLE handle
        );*/
	PacketMonitorUninitialize m_pfnUninitialize{ nullptr };

    friend class Session;
    friend class RealtimeStream;
};

// Session class
class Session : public std::enable_shared_from_this<Session> {
public:
    ~Session();
    
    // Delete copy/move
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    
    void start();
    void stop();
    
    bool isActive() const noexcept { return m_active.load(); }
    const std::wstring& getName() const noexcept { return m_name; }
    HANDLE getHandle() const noexcept { return m_sessionHandle; }
    
    // Create and attach a realtime stream
    std::shared_ptr<RealtimeStream> createRealtimeStream(
        std::shared_ptr<IPacketHandler> handler,
        UINT16 bufferSizeMultiplier = 1,
        UINT16 truncationSize = 0,
        size_t ringBufferSize = 1024);

	//get manager const reference
	const ApiManager& getManager() const noexcept { return m_manager; }

private:
    friend class ApiManager;
    
    Session(const std::wstring& name, HANDLE sessionHandle, ApiManager& manager);
    
    std::wstring m_name;
    HANDLE m_sessionHandle{nullptr};
    ApiManager& m_manager;
    std::atomic<bool> m_active{false};
    std::mutex m_mutex;
    std::vector<std::shared_ptr<RealtimeStream>> m_streams;
};

// Realtime Stream class
class RealtimeStream : public std::enable_shared_from_this<RealtimeStream> {
public:
    ~RealtimeStream();
    
    // Delete copy/move
    RealtimeStream(const RealtimeStream&) = delete;
    RealtimeStream& operator=(const RealtimeStream&) = delete;
    RealtimeStream(RealtimeStream&&) = delete;
    RealtimeStream& operator=(RealtimeStream&&) = delete;
    
    void setHandler(std::shared_ptr<IPacketHandler> handler);
    std::shared_ptr<IPacketHandler> getHandler() const;
    
    PACKETMONITOR_REALTIME_STREAM getHandle() const noexcept { return m_streamHandle; }

    std::shared_ptr<RingBuffer<PacketData>> getRingBuffer() const noexcept { return m_ringBuffer; }

private:
    friend class Session;
    
    RealtimeStream(std::shared_ptr<Session> session,
                   std::shared_ptr<IPacketHandler> handler,
                   UINT16 bufferSizeMultiplier,
                   UINT16 truncationSize,
                   size_t ringBufferSize);
    
    void create();
    void attachToSession();
    
    // Static callbacks
    static VOID CALLBACK onStreamEventCallback(
        _In_opt_ VOID* context,
        _In_ PACKETMONITOR_STREAM_EVENT_INFO const* info,
        _In_ PACKETMONITOR_STREAM_EVENT_KIND kind);
    
    static VOID CALLBACK onStreamDataCallback(
        _In_opt_ VOID* context,
        _In_ PACKETMONITOR_STREAM_DATA_DESCRIPTOR const* data);
    
    std::shared_ptr<Session> m_session;
    std::shared_ptr<IPacketHandler> m_handler;
    PACKETMONITOR_REALTIME_STREAM m_streamHandle{nullptr};
    UINT16 m_bufferSizeMultiplier;
    UINT16 m_truncationSize;
    mutable std::mutex m_mutex;
    std::shared_ptr<RingBuffer<PacketData>> m_ringBuffer;
	size_t m_ringBufferSize;
};

} // namespace Pktmon