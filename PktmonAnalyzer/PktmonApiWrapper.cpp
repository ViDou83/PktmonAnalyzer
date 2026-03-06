#include "PktmonApiWrapper.hpp"
#include "PacketHandlers.hpp"
#include <iostream>

namespace Pktmon {

// ============================================================================
// ApiManager Implementation
// ============================================================================

ApiManager& ApiManager::getInstance() {
    static ApiManager instance;
    return instance;
}

ApiManager::ApiManager() : m_dataSourceCache(std::make_shared<DataSourceCache>()) {
    loadLibrary();
    resolveFunctions();
}

ApiManager::~ApiManager() {
    shutdown();
    if (m_hModule) {
        FreeLibrary(m_hModule);
    }
}

void ApiManager::loadLibrary() {
    m_hModule = LoadLibraryW(L"Pktmonapi.dll");
    if (!m_hModule) {
        throw PktmonException("Failed to load Pktmonapi.dll", HRESULT_FROM_WIN32(GetLastError()));
    }
}

void ApiManager::resolveFunctions() {
    m_pfnInitialize = reinterpret_cast<PfnInitialize>(
        GetProcAddress(m_hModule, "PacketMonitorInitialize"));
    m_pfnCreateSession = reinterpret_cast<PfnCreateSession>(
        GetProcAddress(m_hModule, "PacketMonitorCreateLiveSession"));
    m_pfnSetSessionActive = reinterpret_cast<PfnSetSessionActive>(
        GetProcAddress(m_hModule, "PacketMonitorSetSessionActive"));
    m_pfnCreateRealtimeStream = reinterpret_cast<PfnCreateRealtimeStream>(
        GetProcAddress(m_hModule, "PacketMonitorCreateRealtimeStream"));
    m_pfnAttachOutput = reinterpret_cast<PfnAttachOutput>(
        GetProcAddress(m_hModule, "PacketMonitorAttachOutputToSession"));
    m_pfnEnumDataSources = reinterpret_cast<PfnEnumDataSources>(
        GetProcAddress(m_hModule, "PacketMonitorEnumDataSources"));
    
    if (!m_pfnInitialize || !m_pfnCreateSession || !m_pfnSetSessionActive ||
        !m_pfnCreateRealtimeStream || !m_pfnAttachOutput || !m_pfnEnumDataSources) {
        throw PktmonException("Failed to resolve one or more API functions");
    }
}

void ApiManager::initialize(UINT32 version) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized.load()) {
        return; // Already initialized
    }
    
    HRESULT hr = m_pfnInitialize(version, nullptr, &m_handle);
    if (FAILED(hr)) {
        throw PktmonException("Failed to initialize PacketMonitor API", hr);
    }
    
    m_initialized.store(true);
    
    // Automatically populate the data source cache
    try {
        refreshDataSourceCache();
    } catch (const PktmonException& ex) {
        std::cerr << "Warning: Failed to populate data source cache: " << ex.what() << "\n";
    }
}

void ApiManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_handle) {
        CloseHandle(m_handle);
        m_handle = nullptr;
    }
    
    m_dataSourceCache->clear();
    m_initialized.store(false);
}

std::shared_ptr<Session> ApiManager::createSession(const std::wstring& name) {
    if (!m_initialized.load()) {
        throw PktmonException("API not initialized. Call initialize() first.");
    }
    
    HANDLE sessionHandle = nullptr;
    HRESULT hr = m_pfnCreateSession(m_handle, name.c_str(), &sessionHandle);
    if (FAILED(hr)) {
        throw PktmonException("Failed to create session: " + 
            std::string(name.begin(), name.end()), hr);
    }
    
    return std::shared_ptr<Session>(new Session(name, sessionHandle, *this));
}

std::vector<PACKETMONITOR_DATA_SOURCE_SPECIFICATION> 
ApiManager::enumerateDataSources(PACKETMONITOR_DATA_SOURCE_KIND kind, bool includeHidden) {
    if (!m_initialized.load()) {
        throw PktmonException("API not initialized");
    }
    
    std::vector<BYTE> buffer;
    SIZE_T bytesNeeded = 0;
    HRESULT hr;
    
    do {
        hr = m_pfnEnumDataSources(
            m_handle,
            kind,
            includeHidden ? TRUE : FALSE,
            buffer.size(),
            &bytesNeeded,
            buffer.empty() ? nullptr : reinterpret_cast<PACKETMONITOR_DATA_SOURCE_LIST*>(buffer.data()));
        
        if (FAILED(hr)) {
            throw PktmonException("Failed to enumerate data sources", hr);
        }
        
        if (bytesNeeded > buffer.size()) {
            buffer.resize(bytesNeeded);
        } else {
            break;
        }
    } while (true);
    
    auto* list = reinterpret_cast<PACKETMONITOR_DATA_SOURCE_LIST*>(buffer.data());
    std::vector<PACKETMONITOR_DATA_SOURCE_SPECIFICATION> result;
    result.reserve(list->NumDataSources);
    
    for (UINT32 i = 0; i < list->NumDataSources; ++i) {
        result.push_back(*list->DataSources[i]);
    }
    
    return result;
}

void ApiManager::refreshDataSourceCache() {
    m_dataSourceCache->clear();
    
    auto dataSources = enumerateDataSources();
    
    for (const auto& ds : dataSources) {
        m_dataSourceCache->add(ds);
    }
    
    std::cout << "Data source cache populated with " << m_dataSourceCache->size() << " entries.\n";
}

// ============================================================================
// Session Implementation
// ============================================================================

Session::Session(const std::wstring& name, HANDLE sessionHandle, ApiManager& manager)
    : m_name(name), m_sessionHandle(sessionHandle), m_manager(manager) {}

Session::~Session() {
    stop();
    if (m_sessionHandle) {
        CloseHandle(m_sessionHandle);
    }
}

void Session::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_active.load()) {
        return; // Already active
    }
    
    HRESULT hr = m_manager.m_pfnSetSessionActive(m_sessionHandle, TRUE);
    if (FAILED(hr)) {
        throw PktmonException("Failed to activate session", hr);
    }
    
    m_active.store(true);
}

void Session::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_active.load()) {
        return;
    }
    
    HRESULT hr = m_manager.m_pfnSetSessionActive(m_sessionHandle, FALSE);
    if (FAILED(hr)) {
        std::cerr << "Warning: Failed to deactivate session\n";
    }
    
    m_active.store(false);
}

std::shared_ptr<RealtimeStream> Session::createRealtimeStream(
    std::shared_ptr<IPacketHandler> handler,
    UINT16 bufferSizeMultiplier,
    UINT16 truncationSize) {
    
    // Set the data source cache in the handler
    //handler->setDataSourceCache(m_manager.getDataSourceCache());
    
    auto stream = std::shared_ptr<RealtimeStream>(
        new RealtimeStream(shared_from_this(), handler, bufferSizeMultiplier, truncationSize));
    
    stream->create();
    stream->attachToSession();
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_streams.push_back(stream);
    
    return stream;
}

// ============================================================================
// RealtimeStream Implementation
// ============================================================================

RealtimeStream::RealtimeStream(std::shared_ptr<Session> session,
    std::shared_ptr<IPacketHandler> handler,
    UINT16 bufferSizeMultiplier,
    UINT16 truncationSize)
    : m_session(std::move(session))
    , m_handler(std::move(handler))
    , m_bufferSizeMultiplier(bufferSizeMultiplier)
    , m_truncationSize(truncationSize)
{

}

RealtimeStream::~RealtimeStream() {
    // Cleanup is handled by the API
}

void RealtimeStream::create() {
    PACKETMONITOR_REALTIME_STREAM_CONFIGURATION config{};
    config.UserContext = this;
    config.EventCallback = &RealtimeStream::onStreamEventCallback;
    config.DataCallback = &RealtimeStream::onStreamDataCallback;
    config.BufferSizeMultiplier = m_bufferSizeMultiplier;
    config.TruncationSize = m_truncationSize;
    
    HRESULT hr = m_session->getManager().m_pfnCreateRealtimeStream(
        m_session->getManager().getHandle(),
        &config,
        &m_streamHandle);
    
    if (FAILED(hr)) {
        throw PktmonException("Failed to create realtime stream", hr);
    }
}

void RealtimeStream::attachToSession() {
    HRESULT hr = m_session->getManager().m_pfnAttachOutput(
        m_session->getHandle(),
        m_streamHandle);
    
    if (FAILED(hr)) {
        throw PktmonException("Failed to attach stream to session", hr);
    }
}

void RealtimeStream::setHandler(std::shared_ptr<IPacketHandler> handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handler = std::move(handler);
}

std::shared_ptr<IPacketHandler> RealtimeStream::getHandler() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_handler;
}

VOID CALLBACK RealtimeStream::onStreamEventCallback(
    _In_opt_ VOID* context,
    _In_ PACKETMONITOR_STREAM_EVENT_INFO const* info,
    _In_ PACKETMONITOR_STREAM_EVENT_KIND kind) {
    
    if (!context || !info) return;
    
    auto* stream = static_cast<RealtimeStream*>(context);
    auto handler = stream->getHandler();
    
    if (handler) {
        handler->onStreamEvent(*info, kind);
    }
}

VOID CALLBACK RealtimeStream::onStreamDataCallback(
    _In_opt_ VOID* context,
    _In_ PACKETMONITOR_STREAM_DATA_DESCRIPTOR const* data) {
    
    if (!context || !data) return;
    
    auto* stream = static_cast<RealtimeStream*>(context);
    auto handler = stream->getHandler();
    
    if (handler) {
        handler->onPacketReceived(*data);
    }
}

} // namespace Pktmon