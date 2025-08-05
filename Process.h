#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>

enum class ProcessState {
    NEW,
    READY,
    RUNNING,
    BLOCKED,
    FINISHED
};

struct PageTableEntry {
    bool is_present = false;
    bool is_dirty = false;
    int frame_index = -1;
};

struct MemoryData {
    size_t memory_size_bytes;
    long long creation_timestamp;
    long backing_store_offset;
    std::vector<PageTableEntry> page_table;
    bool terminated_by_error = false;
    std::string termination_reason = "";
    
    std::mutex page_fault_mutex;
    std::condition_variable page_fault_cv;
};

class Process {
public:
    int id;
    std::string processName;
    ProcessState state;
    
    // --- ADDED: For scheduler logic and statistics ---
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

    // --- ADDED: For timing ---
    std::chrono::time_point<std::chrono::system_clock> start_time;
    std::chrono::time_point<std::chrono::system_clock> finish_time;

    MemoryData mem_data;
    
    // --- ADDED: A new constructor that takes a single integer ID ---
    // This constructor matches the call std::make_shared<Process>(cpuClocks++)
    Process(int id) 
        : id(id), processName("P" + std::to_string(id)), state(ProcessState::NEW), 
          arrival_time(0), cpu_burst_time(0), io_burst_time(0), 
          remaining_burst_time(0), io_remaining_time(0), instructions_per_run(0) {}

    // Default constructor
    Process() : id(-1), processName(""), state(ProcessState::NEW), 
                arrival_time(0), cpu_burst_time(0), remaining_burst_time(0),
                io_burst_time(0), io_remaining_time(0), instructions_per_run(0) {}

    // Existing constructor
    Process(int id, const std::string& name, int arrival, int cpu_burst, int io_burst)
        : id(id), processName(name), state(ProcessState::NEW), 
          arrival_time(arrival), cpu_burst_time(cpu_burst), remaining_burst_time(cpu_burst),
          io_burst_time(io_burst), io_remaining_time(io_burst), instructions_per_run(0) {}
};

#endif // PROCESS_H