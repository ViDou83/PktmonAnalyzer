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
void outputThreadFunc(const std::shared_ptr<RingBuffer<std::string>>& outputRingBuffer,
                      std::atomic<bool>& running) {
	//std::cout << "Output thread " << std::this_thread::get_id() << " started\n";
    while (running.load(std::memory_order_acquire) || !outputRingBuffer->empty()) {
        if (auto msg = outputRingBuffer->tryPop()) {
            std::cout << *msg;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    } catch (const std::exception& ex) {
        // ADD THIS: Catch other standard exceptions
        std::cerr << "Error: " << ex.what() << "\n";
    } catch (...) {
        // ADD THIS: Catch unknown exceptions
        std::cerr << "Unknown error occurred\n";
    }
}

void processPacket(const std::shared_ptr<RingBuffer<Pktmon::PacketData>>& packetRingBuffer,
                   const std::shared_ptr<RingBuffer<std::string>>& outputRingBuffer,
                   const std::chrono::steady_clock::time_point& endTime) {
	//std::cout << "Consumer thread " << std::this_thread::get_id() << " started\n";
    while (std::chrono::steady_clock::now() < endTime || !packetRingBuffer->empty()) {
        if (auto entry = packetRingBuffer->tryPop()) {
            // Got a packet - process it
            std::ostringstream oss;
            Pktmon::PacketData& packet = *entry;

            packet.printMetadata(oss);
            packet.printPacketData(oss);
            outputRingBuffer->tryPush(std::move(oss.str()));
        } else {
            // Buffer empty - wait a bit before checking again (avoid busy-spin)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

static void run(const std::shared_ptr<CaptureOptions>& options) {
    // Original single-threaded implementation
    try {
		// Create ring buffers for packets and output messages
        auto outputRingBuffer = std::make_shared<RingBuffer<std::string>>(options->ringBufferSize * 4);

		// Initialize API and create session
        auto& api = Pktmon::ApiManager::getInstance();
        api.initialize(PACKETMONITOR_API_VERSION_1_0);
		// Create a session with a unique name (could also use a GUID or timestamp)
        auto session = api.createSession(L"MyCapture Session");
		// Create and register packet handler that pushes to ring buffer
        auto ringBufferHandler = std::make_shared<Pktmon::RingBufferHandler>(options, api.getDataSourceCache());
		// Create statistics handler to track capture stats
        //auto statsHandler = std::make_shared<Pktmon::StatisticsHandler>();
		// Register both handlers to the session (both get all packets)
        auto stream1 = session->createRealtimeStream(ringBufferHandler, 2, options->truncationSize, options->ringBufferSize);
        //auto stream2 = session->createRealtimeStream(statsHandler, 2, options->truncationSize, options->ringBufferSize);
		// Get the ring buffer from the handler to share with consumer threads
        auto packetRingBuffer = stream1->getRingBuffer();
		// Start the capture session
        session->start();
		// Print capture configuration
        std::cout << "Capturing for " << options->durationSeconds << " seconds";
        if (options->truncationSize > 0) {
            std::cout << " (truncating to " << options->truncationSize << " bytes)";
        }
        if (options->droppedOnly) {
            std::cout << " [DROPPED PACKETS ONLY]";
        }
        std::cout << "\n";
		// Initialize end time for capture duration
        auto endTime = std::chrono::steady_clock::now() + std::chrono::seconds(options->durationSeconds);
        // create a thread to stop the session after capture duration ends (in case session->stop() blocks)
        std::thread stopThread([session, endTime]() {
            std::this_thread::sleep_until(endTime);
            session->stop();
            });
		// Start the output thread 
        std::atomic<bool> outputRunning(true);
        std::thread outputThread(outputThreadFunc, outputRingBuffer, std::ref(outputRunning));
		// Consumer threads to process packets from ring buffer
        std::vector<std::thread> consumerThreads;
		const size_t numThreads = options->useMultiThreaded ? options->numConsumerThreads : 1;
		consumerThreads.reserve(numThreads);
        for (size_t i = 0; i < numThreads; i++) {
            consumerThreads.emplace_back(processPacket, packetRingBuffer, outputRingBuffer, endTime);
        }
		// Wait for stop thread to finish (ensures session is stopped)
        if (stopThread.joinable()) {
            stopThread.join();
		}
		// Wait for capture duration to elapse
        for (auto& thread : consumerThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        // Stop the output thread
		outputRunning.store(false, std::memory_order_release);
		// Wait for output thread to finish processing remaining messages
        if (outputThread.joinable()) {
            outputThread.join();
        }
		// Capture complete - print summary
		std::cout << "\n";
		std::cout << "**** Capture complete ****\n";
        std::cout << "\n";
		// print ring buffers statistics
		std::cout << "=== Packet Ring Buffer ===\n";
		packetRingBuffer->printStatistics();
		std::cout << "\n=== Output Ring Buffer ===\n";
		outputRingBuffer->printStatistics();
		std::cout << "\n=== Capture Statistics ===\n";
		ringBufferHandler->printStatistics();
		// capture complete - print statistics
        //statsHandler->printStatistics();
    } catch (const Pktmon::PktmonException& ex) {
        std::cerr << "Error: " << ex.what() << " (HRESULT: 0x" << std::hex << ex.getHResult() << ")\n";
    } catch (const std::exception& ex) {
        // ADD THIS: Catch other standard exceptions
        std::cerr << "Error: " << ex.what() << "\n";
    } catch (...) {
        // ADD THIS: Catch unknown exceptions
        std::cerr << "Unknown error occurred\n";
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
        << "  -bufsize <count>         : Ring buffer size (default: 2K)\n"
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

    if (argc == 1) {
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