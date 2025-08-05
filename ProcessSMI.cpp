#include <iostream>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <vector>
#include <memory>

#include "ProcessSMI.h"
#include "Process.h"
#include "MemoryManager.h"
#include "global.h"

namespace {

// Helper function to format memory sizes (B, KiB, MiB, etc.)
std::string formatMemory(size_t bytes) {
    const double KiB = 1024.0;
    const double MiB = KiB * 1024.0;
    const double GiB = MiB * 1024.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);

    if (bytes >= GiB) { oss << (bytes / GiB) << " GiB"; }
    else if (bytes >= MiB) { oss << (bytes / MiB) << " MiB"; }
    else if (bytes >= KiB) { oss << (bytes / KiB) << " KiB"; }
    else { oss << bytes << " B"; }
    return oss.str();
}

} // end anonymous namespace

void process_smi::printSnapshot() {
    // Safely check if the system has been initialized.
    if (!g_system_initialized || !memory_manager) {
        std::lock_guard<std::mutex> cout_lk(g_cout_mutex);
        std::cout << "System not initialized. Cannot display process info." << std::endl;
        return;
    }

    constexpr const char* HR = "+----------------------------------------------------------+\n";
    std::ostringstream oss;

    // --- Gather data under locks to ensure a consistent snapshot ---
    int busy_cores = 0;
    size_t used_bytes = 0;
    std::vector<std::shared_ptr<Process>> running_procs_copy;

    // Determine which scheduler's data to use based on the global config string.
    std::mutex& scheduler_mutex = (scheduler == "rr") ? rr_g_process_mutex : fcfs_g_process_mutex;
    const auto& running_list = (scheduler == "rr") ? rr_g_running_processes : fcfs_g_running_processes;

    {
        std::lock_guard<std::mutex> lk(scheduler_mutex);
        // Count busy cores and make a copy of the running processes list to use outside the lock.
        for (const auto& p : running_list) {
            if (p) {
                busy_cores++;
                running_procs_copy.push_back(p);
            }
        }
    }

    // Get total memory usage from the single source of truth: the MemoryManager.
    used_bytes = memory_manager->get_used_memory_bytes();
    size_t total_bytes = static_cast<size_t>(MAX_OVERALL_MEM);

    // Calculate utilization percentages.
    int cpu_util = (CPU_COUNT > 0) ? static_cast<int>(100.0 * busy_cores / CPU_COUNT) : 0;
    int mem_util = (total_bytes > 0) ? static_cast<int>(100.0 * used_bytes / total_bytes) : 0;

    // --- Build the output string using the gathered data ---
    oss << '\n'
        << HR
        << "| PROCESS-SMI v1.0         Driver Version: 1.0           |\n"
        << HR
        << "| CPU-Util: " << std::right << std::setw(3) << cpu_util << "%" << std::string(39, ' ') << "|\n"
        << "| Memory Usage: " << std::left << std::setw(20) << (formatMemory(used_bytes) + " / " + formatMemory(total_bytes))
        << " Memory Util: " << std::right << std::setw(3) << mem_util << "%" << "      |\n"
        << HR
        << "| Running Processes and Memory Usage:                      |\n"
        << HR;

    if (running_procs_copy.empty()) {
        oss << "| No running processes.                                    |\n";
    } else {
        oss << "| " << std::left << std::setw(24) << "PROCESS NAME" << "| " << std::right << std::setw(15) << "MEMORY" << " | " << std::setw(11) << "CORE" << "|\n";
        oss << "|--------------------------+-----------------+------------|\n";
        for (const auto& p : running_procs_copy) {
            oss << "| " << std::left << std::setw(24) << p->processName
                << " | " << std::right << std::setw(15) << formatMemory(p->mem_data.memory_size_bytes)
                << " | " << std::setw(10) << p->assigned_core << " |\n";
        }
    }
    oss << HR << std::flush;

    // Atomically write the entire formatted string to the console.
    std::lock_guard<std::mutex> cout_lk(g_cout_mutex);
    std::cout << oss.str();
}