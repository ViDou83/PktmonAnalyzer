#pragma once

DECLARE_HANDLE(PACKETMONITOR_HANDLE);
DECLARE_HANDLE(PACKETMONITOR_SESSION);
DECLARE_HANDLE(PACKETMONITOR_REALTIME_STREAM);
DECLARE_HANDLE(PACKETMONITOR_PACKET);

typedef UINT64 PACKETMONITOR_METADATA_ID;

#define PACKETMONITOR_METADATA_ID_PROCESSOR_NUMBER 1
#define PACKETMONITOR_METADATA_ID_DATA_SOURCE 2
#define PACKETMONITOR_METADATA_ID_CAPTURE_TIMESTAMP 3

#define PACKETMONITOR_MAX_NAME_LENGTH 64
#define PACKETMONITOR_MAX_STRING_LENGTH 128
#define PACKETMONITOR_IPV6_ADDRESS_SIZE 16
#define PACKETMONITOR_IPV4_ADDRESS_SIZE 4
#define PACKETMONITOR_MAC_ADDRESS_SIZE 6

typedef enum PACKETMONITOR_DATA_SOURCE_KIND
{
    PacketMonitorDataSourceKindAll,
    PacketMonitorDataSourceKindNetworkInterface,
    PacketMonitorDataSourceKindFilterDriver,
    PacketMonitorDataSourceKindProtocol,
    PacketMonitorDataSourceKindApplicationProtocol,
    PacketMonitorDataSourceKindVmSwitch,
    PacketMonitorDataSourceKindUnknown
} PACKETMONITOR_DATA_SOURCE_KIND;

typedef union PACKETMONITOR_IP_ADDRESS
{
    ULONG IPv4;
    UCHAR IPv4_bytes[PACKETMONITOR_IPV4_ADDRESS_SIZE];

    ULONGLONG IPv6[PACKETMONITOR_IPV6_ADDRESS_SIZE / sizeof(ULONGLONG)];
    UCHAR IPv6_bytes[PACKETMONITOR_IPV6_ADDRESS_SIZE];
} PACKETMONITOR_IP_ADDRESS;

typedef struct PACKETMONITOR_DATA_SOURCE_SPECIFICATION
{
    PACKETMONITOR_DATA_SOURCE_KIND Kind;
    WCHAR Name[PACKETMONITOR_MAX_NAME_LENGTH];
    WCHAR Description[PACKETMONITOR_MAX_STRING_LENGTH];

    UINT32 Id;
    UINT32 SecondaryId;

    UINT32 ParentId;

    union
    {
        struct
        {
            UINT Guid : 1;
            UINT IpV6Address : 1;
            UINT IpV4Address : 1;
            UINT MacAddress : 1;

        } IsPresent;

        UINT IsPresentValue;
    };

    union
    {
        GUID Guid;
        PACKETMONITOR_IP_ADDRESS IpAddress;
        UCHAR MacAddress[PACKETMONITOR_MAC_ADDRESS_SIZE];
    } Detail;
} PACKETMONITOR_DATA_SOURCE_SPECIFICATION;

typedef struct PACKETMONITOR_DATA_SOURCE_LIST
{
    UINT32 NumDataSources;
    PACKETMONITOR_DATA_SOURCE_SPECIFICATION const* DataSources[ANYSIZE_ARRAY];
} PACKETMONITOR_DATA_SOURCE_LIST;

typedef struct PACKETMONITOR_PROTOCOL_CONSTRAINT
{
    WCHAR Name[PACKETMONITOR_MAX_NAME_LENGTH];

    union
    {
        struct
        {
            UINT Mac1 : 1;
            UINT Mac2 : 1;
            UINT VlanId : 1;
            UINT EtherType : 1;

            UINT DSCP : 1;
            UINT TransportProtocol : 1;
            UINT Ip1 : 1;
            UINT Ip2 : 1;
            UINT IPv6 : 1; // indicate IP version

            UINT PrefixLength1 : 1;
            UINT PrefixLength2 : 1;

            UINT Port1 : 1;
            UINT Port2 : 1;
            UINT TCPFlags : 1;
            UINT EncapType : 1;
            UINT VxLanPort : 1;

            UINT ClusterHeartbeat : 1;

        } IsPresent;

        UINT IsPresentValue;
    };

    // Ethernet frame
    UCHAR Mac1[PACKETMONITOR_MAC_ADDRESS_SIZE];
    UCHAR Mac2[PACKETMONITOR_MAC_ADDRESS_SIZE];

    USHORT VlanId;

    USHORT EtherType; // aka data link protocol

    // IP header
    USHORT DSCP;
    UCHAR TransportProtocol;

    PACKETMONITOR_IP_ADDRESS Ip1;
    PACKETMONITOR_IP_ADDRESS Ip2;

    UCHAR PrefixLength1;
    UCHAR PrefixLength2;

    // TCP or UDP header
    USHORT Port1;
    USHORT Port2;

    UCHAR TCPFlags;

    // Encapsulation
    ULONG EncapType;
    USHORT VxLanPort;

    // Counters
    UINT64 Packets;
    UINT64 Bytes;
} PACKETMONITOR_PROTOCOL_CONSTRAINT;

#pragma pack(push, 1)
typedef struct PACKETMONITOR_STREAM_METADATA
{
    UINT64  PktGroupId;
    UINT16  PktCount;
    UINT16  AppearanceCount;
    UINT16  DirectionName;
    UINT16  PacketType;
    UINT16  ComponentId;
    UINT16  EdgeId;
    UINT16  FilterId;
    UINT32  DropReason;
    UINT32  DropLocation;
    UINT16  Processor;
    LARGE_INTEGER TimeStamp;
} PACKETMONITOR_STREAM_METADATA;
#pragma pack(pop)

typedef enum PACKETMONITOR_PAYLOAD_KIND
{
    Payload_Unknown,
    Payload_Ethernet,
    Payload_WiFi,
    Payload_IP,
    Payload_HTTP,
    Payload_TCP,
    Payload_UDP,
    Payload_ARP,
    Payload_ICMP,
    Payload_ESP,
    Payload_AH,
    Payload_L4Payload
} PACKETMONITOR_PAYLOAD_KIND;

typedef struct PACKETMONITOR_STREAM_DATA_DESCRIPTOR
{
    _Field_size_bytes_(DataSize) VOID const* Data;
    UINT32 DataSize;

    UINT32 MetadataOffset;
    UINT32 PacketOffset;
    UINT32 PacketLength;
    ULONG MissedPacketWriteCount;
    ULONG MissedPacketReadCount;
} PACKETMONITOR_STREAM_DATA_DESCRIPTOR;

typedef enum PACKETMONITOR_STREAM_EVENT_KIND
{
    PacketMonitorStreamEventStarted,
    PacketMonitorStreamEventStopped,
    PacketMonitorStreamEventFatalError,
    PacketMonitorStreamEventProcessInfo,
} PACKETMONITOR_STREAM_EVENT_KIND;

typedef struct PACKETMONITOR_STREAM_START_INFO_OUT
{
    ULONG PacketBufferSizeInBytes;
    UINT16 TruncationSize;
} PACKETMONITOR_STREAM_START_INFO_OUT;

typedef struct PACKETMONITOR_STREAM_STOP_INFO_OUT
{
    BOOLEAN IsFatalError;
    DWORD Reason;
} PACKETMONITOR_STREAM_STOP_INFO_OUT;

typedef struct PACKETMONITOR_STREAM_PROCESS_INFO_OUT
{
    BOOLEAN IsWarning;
    DWORD Reason;
    UINT64 PacketLength;
} PACKETMONITOR_STREAM_PROCESS_INFO_OUT;

typedef struct PACKETMONITOR_STREAM_EVENT_INFO
{
    union
    {
        PACKETMONITOR_STREAM_START_INFO_OUT StreamStartInfo;
        PACKETMONITOR_STREAM_STOP_INFO_OUT StreamStopInfo;
        PACKETMONITOR_STREAM_PROCESS_INFO_OUT StreamProcessInfo;
    };
} PACKETMONITOR_STREAM_EVENT_INFO;

typedef VOID CALLBACK PACKETMONITOR_STREAM_EVENT_CALLBACK(
    _In_opt_ VOID* context,
    _In_ PACKETMONITOR_STREAM_EVENT_INFO const* StreamEventInfo,
    _In_ PACKETMONITOR_STREAM_EVENT_KIND eventKind
);

typedef VOID CALLBACK PACKETMONITOR_STREAM_DATA_CALLBACK(
    _In_opt_ VOID* context,
    _In_ PACKETMONITOR_STREAM_DATA_DESCRIPTOR const* data
);

typedef struct PACKETMONITOR_REALTIME_STREAM_CONFIGURATION
{
    VOID* UserContext;
    PACKETMONITOR_STREAM_EVENT_CALLBACK* EventCallback;
    PACKETMONITOR_STREAM_DATA_CALLBACK* DataCallback;

    // Buffser size options
    USHORT BufferSizeMultiplier;

    // Packet Logging Config
    UINT16 TruncationSize;
} PACKETMONITOR_REALTIME_STREAM_CONFIGURATION;

#define PACKETMONITOR_API_VERSION_1_0 0x00010000
#define PACKETMONITOR_API_VERSION_CURRENT PACKETMONITOR_API_VERSION_1_0