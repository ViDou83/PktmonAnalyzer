#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include "PktmonApiWrapper.hpp"
#include "PacketHandlers.hpp"
#include "PktmonUtils.h"

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

//void capturePacketsMultiThreaded(const Pktmon::CaptureOptions& options) {
//    try {
//        auto& api = Pktmon::ApiManager::getInstance();
//        api.initialize(PACKETMONITOR_API_VERSION_1_0);
//
//        auto session = api.createSession(L"MultiThreaded Capture Session");
//
//        // Create ring buffer
//        auto ringBuffer = std::make_shared<Pktmon::RingBuffer<Pktmon::PacketData>>(
//            options.ringBufferSize);
//
//        // Create ring buffer handler (producer)
//        auto ringBufferHandler = std::make_shared<Pktmon::RingBufferHandler>(
//            ringBuffer, options, api.getDataSourceCache());
//
//        // Create statistics handler
//        auto statsHandler = std::make_shared<Pktmon::StatisticsHandler>();
//
//        // Create streams
//        auto stream1 = session->createRealtimeStream(ringBufferHandler, 1, options.truncationSize);
//        auto stream2 = session->createRealtimeStream(statsHandler, 1, options.truncationSize);
//
//        // Create multi-threaded processor
//        Pktmon::MultiThreadedPacketProcessor processor(
//            ringBuffer,
//            options.numConsumerThreads,
//            options,
//            api.getDataSourceCache());
//
//        // Start consumer threads
//        processor.start();
//
//        // Start capture
//        session->start();
//
//        std::cout << "Capturing for " << options.durationSeconds << " seconds";
//        if (options.truncationSize > 0) {
//            std::cout << " (truncating to " << options.truncationSize << " bytes)";
//        }
//        if (options.droppedOnly) {
//            std::cout << " [DROPPED PACKETS ONLY]";
//        }
//        std::cout << "\n";
//        std::cout << "Ring buffer size: " << options.ringBufferSize << " packets\n";
//        std::cout << "Consumer threads: " << options.numConsumerThreads << "\n\n";
//
//        // Wait for capture duration
//        std::this_thread::sleep_for(std::chrono::seconds(options.durationSeconds));
//
//        // Stop capture
//        std::cout << "\nStopping capture...\n";
//        session->stop();
//
//        // Give consumers time to drain the buffer
//        std::cout << "Draining ring buffer...\n";
//        while (!ringBuffer->empty()) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//
//        // Stop processor threads
//        processor.stop();
//
//        // Print statistics
//        std::cout << "\n=== Ring Buffer Statistics ===\n";
//        auto rbStats = ringBuffer->getStatistics();
//        std::cout << "Total Pushed: " << rbStats.totalPushed << "\n";
//        std::cout << "Total Popped: " << rbStats.totalPopped << "\n";
//        std::cout << "Dropped (buffer full): " << rbStats.droppedItems << "\n";
//        std::cout << "Handler dropped: " << ringBufferHandler->getDroppedPackets() << "\n";
//
//        std::cout << "\n=== Thread Statistics ===\n";
//        auto threadStats = processor.getStatistics();
//        for (const auto& stat : threadStats) {
//            std::cout << "Thread " << stat.threadId << ": "
//                << stat.packetsProcessed << " packets\n";
//        }
//        std::cout << "Total processed: " << processor.getTotalProcessed() << "\n";
//
//        statsHandler->printStatistics();
//
//    }
//    catch (const Pktmon::PktmonException& ex) {
//        std::cerr << "Error: " << ex.what() << " (HRESULT: 0x"
//            << std::hex << ex.getHResult() << ")\n";
//    }
//}

void capturePackets(const std::shared_ptr<CaptureOptions>& options) {
    if (options->useMultiThreaded) {
        //capturePacketsMultiThreaded(options);
    }
    else {
        // Original single-threaded implementation
        try {
            auto& api = Pktmon::ApiManager::getInstance();
            api.initialize(PACKETMONITOR_API_VERSION_1_0);

            auto session = api.createSession(L"MyCapture Session");

            auto packetHandler = 
                std::make_shared<Pktmon::HexDumpHandler>(options, api.getDataSourceCache());
            auto statsHandler = std::make_shared<Pktmon::StatisticsHandler>();

            auto stream1 = session->createRealtimeStream(packetHandler, 2,options->truncationSize);
            auto stream2 = session->createRealtimeStream(statsHandler, 2, options->truncationSize);

            session->start();

            std::cout << "Capturing for " << options->durationSeconds << " seconds";
            if (options->truncationSize > 0) {
                std::cout << " (truncating to " << options->truncationSize << " bytes)";
            }
            if (options->droppedOnly) {
                std::cout << " [DROPPED PACKETS ONLY]";
            }
            std::cout << "...\n\n";

            std::this_thread::sleep_for(std::chrono::seconds(options->durationSeconds));

            session->stop();
            statsHandler->printStatistics();

        }
        catch (const Pktmon::PktmonException& ex) {
            std::cerr << "Error: " << ex.what() << " (HRESULT: 0x"
                << std::hex << ex.getHResult() << ")\n";
        }
    }
}

void printUsage() {
    std::wcout << L"Usage:\n"
        << L"  -enum                    : Enumerate data sources\n"
        << L"  -capture <seconds>       : Capture packets for specified duration\n"
        << L"  -truncate <bytes>        : Truncate captured packets to specified size\n"
		<< L"                              (0 = no truncation, max 65535)\n"
        << L"  -display <bytes>         : Number of bytes to display in hex dump (0 = no limit, max 65535)\n"
        << L"  -detailed                : Show detailed packet information\n"
        << L"  -nometadata              : Hide metadata in output\n"
        << L"  -drop                    : Show only dropped packets\n"
        << L"  -threads <count>         : Number of consumer threads (enables multi-threaded mode)\n"
        << L"  -bufsize <count>         : Ring buffer size (default: 10000)\n"
        << L"\n"
        << L"Examples:\n"
        << L"  PktmonAnalyzer.exe -enum\n"
        << L"  PktmonAnalyzer.exe -capture 30\n"
        << L"  PktmonAnalyzer.exe -capture 60 -threads 4\n"
        << L"  PktmonAnalyzer.exe -capture 60 -threads 4 -bufsize 50000\n"
        << L"  PktmonAnalyzer.exe -capture 60 -drop -detailed -threads 2\n";
}

int wmain(int argc, wchar_t* argv[]) {

    std::shared_ptr<CaptureOptions> options = std::make_shared<CaptureOptions>();

    if (argc == 1)
    {
        capturePackets(options);
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
        
        capturePackets(options);
        return 0;
    }
    
    std::wcerr << L"Unknown option: " << argv[1] << L"\n";
    printUsage();
    return 1;
}