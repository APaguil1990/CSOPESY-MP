#include <iostream> 
#include <iomanip> 
#include <sstream> 
#include <mutex> 
#include <vector> 
#include <memory> 

#include "ProcessSMI.h" 
// --- MODIFIED: Include the new core headers ---
#include "Process.h"
#include "MemoryManager.h"
#include "global.h"

namespace { // The anonymous namespace for your helper function is good practice.

// Your formatMemory function is perfect, no changes needed here.
std::string formatMemory(std::size_t bytes) {
    const double KiB = 1024.0; 
    const double MiB = KiB * 1024; 
    const double GiB = MiB * 1024.0; 
    std::ostringstream oss; 
    if (bytes < KiB) { oss << bytes << "B"; } 
    else if (bytes < MiB) { oss << std::fixed << std::setprecision(1) << (bytes / KiB) << "KiB"; } 
    else if (bytes < GiB) { oss << std::fixed << std::setprecision(1) << (bytes / MiB) << "MiB"; } 
    else { oss << std::fixed << std::setprecision(1) << (bytes / GiB) << "GiB"; }
    return oss.str();
}

} // end anonymous namespace

// --- MODIFIED: This is the updated printSnapshot function ---
void process_smi::printSnapshot() {
    constexpr const char* HR = "-------------------------------------------------------------\n";
    std::ostringstream oss;

    /* ── Gather data under lock ─────────────────────────────── */
    int busyCores           = 0;
    std::size_t usedBytes; // Will get this from the MemoryManager
    std::vector<std::shared_ptr<Process>> running_copy;

    {
        std::lock_guard<std::mutex> lk(rr_g_process_mutex);

        // Count busy cores from the running processes list (same as your original)
        for (const auto& p : rr_g_running_processes) {
            if (p) ++busyCores;
        }
        
        // --- NEW: Get total used memory from the MemoryManager ---
        // This replaces the loop over the deleted 'rr_g_memory_processes'
        usedBytes = memory_manager->get_used_memory_bytes();
        // Make a shallow copy of the running processes to print outside the lock
        running_copy = rr_g_running_processes;
    }

    int cpuUtil   = CPU_COUNT ? static_cast<int>(100.0 * busyCores / CPU_COUNT) : 0;
    std::size_t totalBytes = static_cast<std::size_t>(MAX_OVERALL_MEM);
    int memUtil   = totalBytes ? static_cast<int>(100.0 * usedBytes / totalBytes) : 0;

    /* ── Build the pretty string (your layout is preserved) ───── */
    oss << '\n'
        << HR
        << "| PROCESS-SMI V01.00  Driver Version: 01.00 |\n\n"
        << "CPU-Util:  "    << std::setw(3) << cpuUtil   << "%\n"
        << "Memory Usage: " << formatMemory(usedBytes)
        << " / "            << formatMemory(totalBytes)  << '\n'
        << "Memory Util: "  << std::setw(3) << memUtil   << "%\n\n"
        << "Running Processes and Memory Usage:\n"
        << HR;

    // --- MODIFIED: Iterate over the copy of 'Process' pointers ---
    for (const auto& p : running_copy) {
        if (!p) continue;
        // The member variable is now p->mem_data.memory_size_bytes
        oss << std::left << std::setw(12) << p->processName
            << formatMemory(p->mem_data.memory_size_bytes) << '\n';
    }
    oss << HR << std::flush;

    /* ── Atomically write to console ─────────────────────────── */
    std::lock_guard<std::mutex> cout_lk(g_cout_mutex);
    std::cout << oss.str() << std::endl;
}