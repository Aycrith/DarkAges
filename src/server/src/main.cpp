// DarkAges MMO Server - Main Entry Point
// [DATABASE_AGENT] Server initialization with Redis/ScyllaDB integration

#include "zones/ZoneServer.hpp"
#include "zones/ZoneDefinition.hpp"
#include "Constants.hpp"
#include <iostream>
#include <cstdlib>
#include <string>

using namespace DarkAges;

void printUsage(const char* programName) {
    std::cout << "DarkAges Server v" << Constants::VERSION << "\n"
              << "Usage: " << programName << " [options]\n"
              << "\nOptions:\n"
              << "  --port <num>          Server port (default: 7777)\n"
              << "  --zone-id <num>       Zone ID (default: 1)\n"
              << "  --redis-host <host>   Redis host (default: localhost)\n"
              << "  --redis-port <num>    Redis port (default: 6379)\n"
              << "  --scylla-host <host>  ScyllaDB host (default: localhost)\n"
              << "  --scylla-port <num>   ScyllaDB port (default: 9042)\n"
              << "  --help, -h            Show this help\n";
}

int main(int argc, char* argv[]) {
    try {
        // Default configuration
        ZoneConfig config;
        config.zoneId = 1;
        config.port = Constants::DEFAULT_SERVER_PORT;
        config.redisHost = "localhost";
        config.redisPort = Constants::REDIS_DEFAULT_PORT;
        config.scyllaHost = "localhost";
        config.scyllaPort = Constants::SCYLLA_DEFAULT_PORT;
        config.minX = Constants::WORLD_MIN_X;
        config.maxX = Constants::WORLD_MAX_X;
        config.minZ = Constants::WORLD_MIN_Z;
        config.maxZ = Constants::WORLD_MAX_Z;
        config.auraBuffer = Constants::AURA_BUFFER_METERS;
        
        // Parse command line arguments
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            
            if (arg == "--help" || arg == "-h") {
                printUsage(argv[0]);
                return 0;
            } else if (arg == "--port" && i + 1 < argc) {
                config.port = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else if (arg == "--zone-id" && i + 1 < argc) {
                config.zoneId = static_cast<uint32_t>(std::atoi(argv[++i]));
            } else if (arg == "--redis-host" && i + 1 < argc) {
                config.redisHost = argv[++i];
            } else if (arg == "--redis-port" && i + 1 < argc) {
                config.redisPort = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else if (arg == "--scylla-host" && i + 1 < argc) {
                config.scyllaHost = argv[++i];
            } else if (arg == "--scylla-port" && i + 1 < argc) {
                config.scyllaPort = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else {
                std::cerr << "Unknown option: " << arg << "\n";
                printUsage(argv[0]);
                return 1;
            }
        }
        
        std::cout << R"(
    ____             __                ___    ____  _____
   / __ \____ ______/ /_____  _____   /   |  / __ \/ ___/
  / / / / __ `/ ___/ //_/ _ \/ ___/  / /| | / /_/ /\__ \ 
 / /_/ / /_/ / /  / ,< /  __/ /     / ___ |/ _, _/___/ / 
/_____/\__,_/_/  /_/|_|\___/_/     /_/  |_/_/ |_|/____/  
                                                          
)" << "\n";
        
        std::cout << "Version: " << Constants::VERSION << "\n";
        std::cout << "Zone ID: " << config.zoneId << "\n";
        std::cout << "Port: " << config.port << "\n";
        std::cout << "Redis: " << config.redisHost << ":" << config.redisPort << "\n";
        std::cout << "ScyllaDB: " << config.scyllaHost << ":" << config.scyllaPort << "\n";
        std::cout << "World Bounds: [" << config.minX << ", " << config.maxX << "] x [" 
                  << config.minZ << ", " << config.maxZ << "]\n";
        std::cout << "\nInitializing server...\n\n";
        
        // Create and initialize server
        ZoneServer server;
        
        if (!server.initialize(config)) {
            std::cerr << "\nFailed to initialize server. Check logs for details.\n";
            return 1;
        }
        
        std::cout << "\n========================================\n";
        std::cout << "Server is running!\n";
        std::cout << "Press Ctrl+C to stop\n";
        std::cout << "========================================\n\n";
        
        // Run main loop (blocks until shutdown)
        server.run();
        
        std::cout << "\nServer shutdown complete.\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\nFatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "\nUnknown fatal error\n";
        return 1;
    }
}
