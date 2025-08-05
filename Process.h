#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <chrono>
#include <atomic>

class MemoryManager;
// --- Unified Process State ---
enum class ProcessState {
    READY,
    RUNNING,
    BLOCKED, // New state for processes waiting on a page fault
    FINISHED
};

// --- Memory Management Data per Process ---
struct PageTableEntry {
    bool is_present = false;
    bool is_dirty = false;
    int frame_index = -1;
};

struct ProcessMemoryData {
    size_t memory_size_bytes = 0;
    long backing_store_offset = -1;
    std::vector<PageTableEntry> page_table;
    long long creation_timestamp = 0;
    
    // For 'screen -r' error reporting
    bool terminated_by_error = false;
    std::string termination_reason = "";
};

// --- The Unified Process Control Block (PCB) ---
// This class replaces RR_PCB and FCFS_PCB.
class Process {
public:
    // --- Core Fields (from both RR_PCB and FCFS_PCB) ---
    int id = 0;
    std::string processName = "";
    std::atomic<ProcessState> state;
    std::vector<std::string> commands;
    size_t program_counter = 0;
    size_t memory_size = 0;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point finish_time;
    int assigned_core = -1;
    std::vector<std::string> log_file;

    // --- RR-Specific Field ---
    int commands_executed_this_quantum = 0;

    // --- Memory Management Data ---
    ProcessMemoryData mem_data;

    // --- Constructor ---
    Process(int pid) : id(pid), state(ProcessState::READY) {}
};

#endif // PROCESS_H