#pragma once 
#include <string> 
#include <vector> 
#include <memory> 
#include <unordered_set> 
#include <mutex> 
#include <chrono>

// Public enums 
enum class ProcessState {
    READY, 
    RUNNING, 
    FINISHED
}; 

// Public PCB 
struct RR_PCB {
    int id = 0; 
    std::string processName = ""; 
    int commands_executed_this_quantum = 0; 
    ProcessState state = ProcessState::READY; 
    std::vector<std::string> commands; 
    std::size_t program_counter = 0; 
    std::chrono::system_clock::time_point start_time; 
    std::chrono::system_clock::time_point finish_time; 
    int assigned_core = -1; 
    std::vector<std::string> log_file; 
    std::size_t memory_size;

    std::vector<std::tuple<std::string, uint16_t>> variables;
}; 

extern std::vector<std::shared_ptr<RR_PCB>> rr_g_running_processes; 
extern std::unordered_set<std::shared_ptr<RR_PCB>> rr_g_memory_processes; 
extern std::mutex rr_g_process_mutex; 

extern int CPU_COUNT; 
extern int MIN_MEM_PER_PROC;
extern int MAX_MEM_PER_PROC;
extern int MAX_OVERALL_MEM; 

// Test helper to return snapshot of running-process names 
std::vector<std::string> rr_getRunningProcessNames();

class MemoryManager; // Forward declaration is enough

// --- Public Function Declarations ONLY ---
int RR();
void rr_create_processes(MemoryManager& mm);
void rr_display_processes();
void rr_write_processes();
std::vector<std::string> rr_getRunningProcessNames();
