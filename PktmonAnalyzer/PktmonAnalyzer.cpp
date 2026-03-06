// Windows
#include <windows.h>

// STL
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

//local
#include "Pktmonapi.hpp"
#include "PktmonApiWrapper.hpp"
#include "PacketHandlers.hpp"
#include "PktmonUtils.h"
#include "RingBuffer.hpp"
#include "PacketData.hpp"

// Dedicated output thread - uses lock-free RingBuffer for messages
void outputThreadFunc(const std::shared_ptr<RingBuffer<std::string>>& outputQueue,
                      std::atomic<bool>& running) {
    while (running.load(std::memory_order_acquire) || true) {
        if (auto msg = outputQueue->tryPop()) {
            std::cout << *msg;
        }
        else {
            if (!running.load(std::memory_order_acquire)) {
                break;  // Exit only when stopped AND queue is empty
            }
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

void printDataSources() {
    try {
        auto& api = Pktmon::ApiManager::getInstance();
        api.initialize(PACKETMONITOR_API_VERSION_1_0);
        auto dataSources = api.enumerateDataSources();
        
        std::wcout << L"Found " << dataSources.size() << L" data sources:\n\n";
        
        for (const auto& ds : dataSources) {
            std::wcout << L"Name: " << ds.Name << L"\n"
                      << L"Description: " << ds.Description << L"\n"
                      << L"ID: " << ds.Id << L"\n\n";
        }
    } catch (const Pktmon::PktmonException& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
    }
}

void processPacket(const std::shared_ptr<RingBuffer<Pktmon::PacketData>>& packetRingBuffer,
                   const std::shared_ptr<RingBuffer<std::string>>& outputRingBuffer,
                   const std::chrono::steady_clock::time_point& endTime) {
    while (std::chrono::steady_clock::now() < endTime) {
        if (auto entry = packetRingBuffer->tryPop()) {
            // Got a packet - process it
            std::ostringstream oss;
            Pktmon::PacketData& packet = *entry;

            packet.printMetadata(oss);
            packet.printPacketData(oss);

            std::string output = oss.str();
            outputRingBuffer->tryPush(std::move(output));
        }
        else {
            // Buffer empty - wait a bit before checking again (avoid busy-spin)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

static void run(const std::shared_ptr<CaptureOptions>& options) {
    // Original single-threaded implementation
    try {
        auto& api = Pktmon::ApiManager::getInstance();
        api.initialize(PACKETMONITOR_API_VERSION_1_0);

        auto session = api.createSession(L"MyCapture Session");

		auto packetRingBuffer = std::make_shared<RingBuffer<Pktmon::PacketData>>(options->ringBufferSize);

        auto ringBufferHandler = std::make_shared<Pktmon::RingBufferHandler>(
            packetRingBuffer, options, api.getDataSourceCache());

        auto outputRingBuffer = std::make_shared<RingBuffer<std::string>>(options->ringBufferSize);

        auto statsHandler = std::make_shared<Pktmon::StatisticsHandler>();

        auto stream1 = session->createRealtimeStream(ringBufferHandler, 2,options->truncationSize);
        auto stream2 = session->createRealtimeStream(statsHandler, 2, options->truncationSize);

        session->start();

        std::cout << "Capturing for " << options->durationSeconds << " seconds";
        if (options->truncationSize > 0) {
            std::cout << " (truncating to " << options->truncationSize << " bytes)";
        }
        if (options->droppedOnly) {
            std::cout << " [DROPPED PACKETS ONLY]";
        }

        std::cout << "\n";

        // Process packets in real-time as they arrive (time-based loop)
        auto endTime = std::chrono::steady_clock::now() + std::chrono::seconds(options->durationSeconds);


        std::atomic<bool> outputRunning(true);
        std::thread outputThread(outputThreadFunc, outputRingBuffer, std::ref(outputRunning));

		std::vector<std::thread> consumerThreads;

        for (int i = 0; i < options->numConsumerThreads; i++) {
            std::thread consumerThread(processPacket, packetRingBuffer, outputRingBuffer, endTime);
            consumerThreads.push_back(std::move(consumerThread));
        }

        for (auto& thread : consumerThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        session->stop();

        // Drain any remaining packets after capture period ends
        std::cout << "Draining remaining packets...\n";
        while (auto entry = packetRingBuffer->tryPop()) {
            std::ostringstream oss;

            Pktmon::PacketData& packet = *entry;

            packet.printMetadata(oss);
            packet.printPacketData(oss);

            std::string output = oss.str();
            outputRingBuffer->tryPush(std::move(output));
        }
        
        // Stop the output thread
        outputRunning = false;
        if (outputThread.joinable()) {
            outputThread.join();
        }
    
        std::cout << "...\n\n";

        statsHandler->printStatistics();

    }
    catch (const Pktmon::PktmonException& ex) {
        std::cerr << "Error: " << ex.what() << " (HRESULT: 0x"
            << std::hex << ex.getHResult() << ")\n";
    }
}

void printUsage() {
    std::cout << "Usage:\n"
        << "  -enum                    : Enumerate data sources\n"
        << "  -capture <seconds>       : Capture packets for specified duration\n"
        << "  -truncate <bytes>        : Truncate captured packets to specified size\n"
		<< "                              (0 = no truncation, max 65535)\n"
        << "  -display <bytes>         : Number of bytes to display in hex dump (0 = no limit, max 65535)\n"
        << "  -detailed                : Show detailed packet information\n"
        << "  -nometadata              : Hide metadata in output\n"
        << "  -drop                    : Show only dropped packets\n"
        << "  -threads <count>         : Number of consumer threads (enables multi-threaded mode)\n"
        << "  -bufsize <count>         : Ring buffer size (default: 10000)\n"
        << "\n"
        << "Examples:\n"
        << "  PktmonAnalyzer.exe -enum\n"
        << "  PktmonAnalyzer.exe -capture 30\n"
        << "  PktmonAnalyzer.exe -capture 60 -threads 4\n"
        << "  PktmonAnalyzer.exe -capture 60 -threads 4 -bufsize 50000\n"
        << "  PktmonAnalyzer.exe -capture 60 -drop -detailed -threads 2\n";
}

int wmain(int argc, wchar_t* argv[]) {

    std::shared_ptr<CaptureOptions> options = std::make_shared<CaptureOptions>();

    if (argc == 1)
    {
        run(options);
        return 0;
	}

    if (wcscmp(argv[1], L"-enum") == 0) {
        printDataSources();
        return 0;
    } else if (wcscmp(argv[1], L"-capture") == 0) {
        if (argc < 3) {
            std::wcerr << L"Error: -capture requires duration argument\n";
            printUsage();
            return 1;
        }
        
        try {
            options->durationSeconds = std::stoi(argv[2]);
            
            if (options->durationSeconds <= 0) {
                std::wcerr << L"Error: Duration must be positive\n";
                throw std::invalid_argument("Invalid duration");
            }
            
            // Parse optional parameters
            for (int i = 3; i < argc; i++) {
                if (wcscmp(argv[i], L"-truncate") == 0) {
                    if (i + 1 >= argc) {
                        std::wcerr << L"Error: -truncate requires size argument\n";
                        throw std::invalid_argument("Invalid option");
                    }
                    int truncateValue = std::stoi(argv[i + 1]);
                    if (truncateValue < 0 || truncateValue > 65535) {
                        std::wcerr << L"Error: Truncate size must be 0-65535\n";
                        throw std::invalid_argument("Invalid option");
                    }
                    options->truncationSize = static_cast<UINT16>(truncateValue);
                    i++;
                } else if (wcscmp(argv[i], L"-display") == 0) {
                    if (i + 1 >= argc) {
                        std::wcerr << L"Error: -display requires size argument\n";
                        throw std::invalid_argument("Invalid option");
                    }
                    int displayValue = std::stoi(argv[i + 1]);
                    if (displayValue < 0 || displayValue > 65535) {
                        std::wcerr << L"Error: Display size must be 0-65535\n";
                        throw std::invalid_argument("Invalid option");
                    }
                    options->displayLength = static_cast<UINT16>(displayValue);
                    i++;
                } else if (wcscmp(argv[i], L"-detailed") == 0) {
                    options->showDetailedMetadata = true;
                } else if (wcscmp(argv[i], L"-nometadata") == 0) {
                    options->showDetailedMetadata = false;
                } else if (wcscmp(argv[i], L"-drop") == 0) {
                    options->droppedOnly = true;
                } else if (wcscmp(argv[i], L"-threads") == 0) {
                    if (i + 1 >= argc) {
                        std::wcerr << L"Error: -threads requires count argument\n";
                        throw std::invalid_argument("Invalid option");
                    }
                    options->numConsumerThreads = std::stoull(argv[i + 1]);
                    options->useMultiThreaded = true;
                    i++;
                } else if (wcscmp(argv[i], L"-bufsize") == 0) {
                    if (i + 1 >= argc) {
                        std::wcerr << L"Error: -bufsize requires count argument\n";
                        throw std::invalid_argument("Unknown option");
                    }
                    options->ringBufferSize = std::stoull(argv[i + 1]);
                    i++;
                } else {
                    std::wcerr << L"Unknown option: " << argv[i] << L"\n";
                    throw std::invalid_argument("Unknown option");
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "Error parsing arguments: " << ex.what() << "\n";
            printUsage();
            return 1;
        }
        
        run(options);
        return 0;
    }
    
    std::wcerr << L"Unknown option: " << argv[1] << L"\n";
    printUsage();
    return 1;
}