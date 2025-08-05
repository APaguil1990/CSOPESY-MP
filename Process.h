#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <cstdint> // For uint16_t

enum class ProcessState {
    NEW,
    READY,
    RUNNING,
    BLOCKED,
    FINISHED,
    TERMINATED // ADDED: For error state
};

struct PageTableEntry {
    bool is_present = false;
    bool is_dirty = false;
    int frame_index = -1;
};

// ADDED: Memory data now includes comprehensive error and state info
struct MemoryData {
    size_t memory_size_bytes;
    long long creation_timestamp;
    long backing_store_offset;
    std::vector<PageTableEntry> page_table;
    
    // --- Error handling members for 'screen -r' ---
    bool terminated_by_error = false;
    std::string termination_reason = "";
    std::chrono::system_clock::time_point termination_time;
    int invalid_address = -1;
    
    // Mutex and CV for handling page faults cleanly
    std::mutex page_fault_mutex;
    std::condition_variable page_fault_cv;
};

class Process {
public:
    int id;
    std::string processName;
    ProcessState state;
    
    // Scheduler and statistics
    int arrival_time;
    int cpu_burst_time;
    int io_burst_time;
    int remaining_burst_time;
    int io_remaining_time;
    int instructions_per_run;
    int assigned_core = -1;
    int program_counter = 0;
    int commands_executed_this_quantum = 0;
    std::vector<std::string> commands;

    // Timing
    std::chrono::time_point<std::chrono::system_clock> start_time;
    std::chrono::time_point<std::chrono::system_clock> finish_time;

    // Memory and variables
    MemoryData mem_data;
    std::unordered_map<std::string, uint16_t> variables; // ADDED: Symbol table for DECLARE, ADD etc.
    std::vector<std::string> output_logs; // ADDED: To store logs for PRINT

    // Constructor
    Process(int id_val, const std::string& name = "") 
        : id(id_val),
          processName(name.empty() ? "P" + std::to_string(id_val) : name),
          state(ProcessState::NEW), 
          arrival_time(0), cpu_burst_time(0), io_burst_time(0), 
          remaining_burst_time(0), io_remaining_time(0), instructions_per_run(0) {
        start_time = std::chrono::system_clock::now();
    }
};

#endif // PROCESS_H