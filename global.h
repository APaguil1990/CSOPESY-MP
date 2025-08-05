#ifndef GLOBAL_H
#define GLOBAL_H

#include <vector>
#include <deque>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstddef>

// Forward declare complex types to avoid including full headers
#include "Process.h"
#include "MemoryManager.h"

// --- EXTERN DECLARATIONS FOR ALL GLOBALS ---

// Memory Manager
extern MemoryManager* memory_manager;

// Creation Queue
struct ProcessCreationRequest {
    std::string name;
    size_t memory_size;
};
extern std::deque<ProcessCreationRequest> g_creation_queue;
extern std::atomic<bool> g_system_initialized;
extern std::mutex g_cout_mutex;

// RR Scheduler Globals
extern std::deque<std::shared_ptr<Process>> rr_g_ready_queue;
extern std::vector<std::shared_ptr<Process>> rr_g_running_processes;
extern std::vector<std::shared_ptr<Process>> rr_g_finished_processes;
extern std::deque<std::shared_ptr<Process>> rr_g_blocked_queue;
extern std::mutex rr_g_process_mutex;
extern std::condition_variable rr_g_scheduler_cv;
extern std::atomic<bool> rr_g_is_running;

// FCFS Scheduler Globals
extern std::deque<std::shared_ptr<Process>> fcfs_g_ready_queue;
extern std::vector<std::shared_ptr<Process>> fcfs_g_running_processes;
extern std::vector<std::shared_ptr<Process>> fcfs_g_finished_processes;
extern std::deque<std::shared_ptr<Process>> fcfs_g_blocked_queue;
extern std::mutex fcfs_g_process_mutex;
extern std::condition_variable fcfs_g_scheduler_cv;
extern std::atomic<bool> fcfs_g_is_running;

// Other global variables
extern int CPU_COUNT;
extern bool process_maker_running;
extern int cpuClocks;
extern std::string scheduler;
extern int qCycles;
extern int processFrequency;
extern int MIN_INS;
extern int MAX_INS;
extern int delayPerExec;
extern int MAX_OVERALL_MEM;
extern int MEM_PER_FRAME;
extern int MIN_MEM_PER_PROC;
extern int MAX_MEM_PER_PROC;
extern unsigned short variable_a;
extern unsigned short variable_b;
extern unsigned short variable_c;

#endif // GLOBALS_H