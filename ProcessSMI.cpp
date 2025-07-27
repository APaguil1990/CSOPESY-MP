#include <iostream> 
#include <iomanip> 
#include <sstream> 
#include <mutex> 
#include <vector> 
#include <unordered_set> 
#include <memory> 

#include "ProcessSMI.h" 
#include "RR.h"

extern int CPU_COUNT; 
// extern int MEM_PER_PROC; 
extern int MAX_OVERALL_MEM; 

enum class ProcessState; 
struct RR_PCB; 
extern std::vector<std::shared_ptr<RR_PCB>> rr_g_running_processes; 
extern std::unordered_set<std::shared_ptr<RR_PCB>> rr_g_memory_processes; 
extern std::mutex rr_g_process_mutex; 

namespace {

// std::string toMiB(std::size_t bytes) {
//     std::ostringstream oss; 
//     oss << std::fixed << std::setprecision(0) << (bytes / 1024.0 / 1024.0) << "MiB";
//     return oss.str();
// }

std::string formatMemory(std::size_t bytes) {
    const double KiB = 1024.0; 
    const double MiB = KiB * 1024; 
    const double GiB = MiB * 1024.0; 

    std::ostringstream oss; 

    if (bytes < KiB) {
        oss << bytes << "B"; 
    } else if (bytes < MiB) {
        double value = bytes / KiB; 

        if (value == static_cast<int>(value)) {
            oss << static_cast<int>(value) << "KiB"; 
        } else {
            oss << std::fixed << std::setprecision(1) << value << "KiB";
        }
    } else if (bytes < GiB) {
        double value = bytes / MiB; 

        if (value == static_cast<int>(value)) {
            oss << static_cast<int>(value) << "MiB"; 
        } else {
            oss << std::fixed << std::setprecision(1) << value << "MiB";
        }
    } else {
        double value = bytes / GiB; 

        if (value == static_cast<int>(value)) {
            oss << static_cast<int>(value) << "GiB"; 
        } else {
            oss << std::fixed << std::setprecision(1) << value << "GiB";
        } 
    } 
    return oss.str();
}

}

void process_smi::printSnapshot() {
    constexpr const char* HR = "-------------------------------------------------------------\n"; 
    std::lock_guard<std::mutex> lock(rr_g_process_mutex); 

    // CPU 
    int busyCores = 0; 
    for (const auto& p : rr_g_running_processes) if (p) ++busyCores;
    int cpuUtil = CPU_COUNT ? static_cast<int>(100.0 * busyCores / CPU_COUNT) : 0; 

    // Memory 
    std::size_t usedBytes = 0; 
    for (const auto& p : rr_g_memory_processes) {
        if (p) usedBytes += p->memory_size;
    } 
    auto totalBytes = static_cast<std::size_t>(MAX_OVERALL_MEM); 
    int memUtil = totalBytes ? static_cast<int>(100.0 * usedBytes / totalBytes) : 0;

    // Output
    std::cout << '\n' << HR 
              << "| PROCESS-SMI V01.00 Driver Version: 01.00 | \n\n" 
              << "CPU-Util:  "      << std::setw(3) << cpuUtil << "%\n" 
              << "Memory Usage: "   << formatMemory(usedBytes) 
              << " / "              << formatMemory(totalBytes) << '\n' 
              << "Memory Util: "    << std::setw(3) << memUtil << "%\n\n" 
              << "Running Processes and Memory Usage:\n" 
              << HR;
    
    // List running processes 
    for (const auto& p : rr_g_running_processes) {
        if (!p) continue; 
        std::cout << std::left << std::setw(12) << p->processName << formatMemory(p->memory_size) << '\n'; 
    }
    std::cout << HR << std::flush;
    
    // constexpr const char* HR = "-------------------------------------------------------------\n"; 
    // std::lock_guard<std::mutex> lock(rr_g_process_mutex); 

    // // Metrics 
    // int busyCores = 0; 
    // for (const auto& p : rr_g_running_processes) if (p) ++busyCores; 

    // int cpuUtil = CPU_COUNT ? static_cast<int>(100.0 * busyCores / CPU_COUNT) : 0; 
    // auto usedBytes = rr_g_memory_processes.size() * static_cast<std::size_t>(MEM_PER_PROC); 
    // auto totalBytes = static_cast<std::size_t>(MAX_OVERALL_MEM); 
    // int memUtil = totalBytes ? static_cast<int>(100.0 * usedBytes / totalBytes) : 0; 

    // // Print 
    // std::cout << '\n' << HR 
    //           << "| PROCESS-SMI V01.00 Driver Version: 01.00 |\n\n" 
    //           << "CPU-Util:  "      << std::setw(3) << cpuUtil << "%\n" 
    //           << "Memory Usage: "   << toMiB(usedBytes) 
    //           << " / "              << toMiB(totalBytes) << '\n' 
    //           << "Memory Util: "    << std::setw(3) << memUtil << "%\n\n" 
    //           << "Running Processes and Memory Usage:\n" 
    //           << HR; 

    // for (const auto& p : rr_g_running_processes) {
    //     if (!p) continue; 
    //     std::cout << std::left << std::setw(12) << p->processName << toMiB(MEM_PER_PROC) << '\n'; 
    // }
    // std::cout << HR << std::flush;
}