#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iomanip>
#include <deque>
#include <fstream>
#include <memory>
#include <atomic>
#include <sstream>
#include <random>

#include "global.h"
#include "vmstat.h"
#include "FCFS.h"
#include "Process.h"
#include "MemoryManager.h"
#include "ScreenManager.h"
#include "config.h"

// --- Configuration ---
const int NUM_PROCESSES = 10;
const int COMMANDS_PER_PROCESS = 100;



std::random_device rd;  // a seed source for the random number engine
std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()

// --- Helper Function to Format Time ---
std::string fcfs_format_time(const std::chrono::system_clock::time_point& tp, const std::string& fmt) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, fmt.c_str());
    return ss.str();
}

void fcfs_declareCommand() {
    std::uniform_int_distribution<> declare_rand(0, 2);

    int declared = declare_rand(gen);

    switch (declared) {
        case 0:
            variable_a = 1;
            break;
        case 1:
            variable_b = 1;
            break;
        case 2:
            variable_c = 1;
            break;
    }
}

void fcfs_addCommand() {
    variable_a = variable_b + variable_c;
}

void fcfs_subtractCommand() {
    variable_a = variable_b - variable_c;
}

void fcfs_sleepCommand() {
    cpuClocks += 10;
}

void fcfs_forCommand() {
    for (int i; i < 5; i++) {
        fcfs_addCommand();
    }
}

// --- Scheduler Thread Function ---
// --- MODIFIED: Scheduler now handles unblocking processes ---
void fcfs_scheduler_thread_func() {
    while (fcfs_g_is_running) {
        std::unique_lock<std::mutex> lock(fcfs_g_process_mutex);

        // --- NEW: Unblock processes that finished I/O ---
        // This simulates the OS getting a notification from the disk
        // that a page has been loaded into RAM.
        while (!fcfs_g_blocked_queue.empty()) {
            std::shared_ptr<Process> unblocked_process = fcfs_g_blocked_queue.front();
            fcfs_g_blocked_queue.pop_front();
            unblocked_process->state = ProcessState::READY;
            fcfs_g_ready_queue.push_back(unblocked_process);
        }

        // --- ORIGINAL LOGIC (Unchanged) ---
        // The condition to wake up and schedule is the same.
        fcfs_g_scheduler_cv.wait(lock, [&]() {
            if (!fcfs_g_is_running) return true;
            bool core_is_free = false;
            for (const auto& p : fcfs_g_running_processes) {
                if (p == nullptr) {
                    core_is_free = true;
                    break;
                }
            }
            return core_is_free && !fcfs_g_ready_queue.empty();
        });

        if (!fcfs_g_is_running) break;

        // --- MODIFIED: Assigns the new 'Process' type ---
        // The scheduling logic itself is the same as your original.
        for (int i = 0; i < CPU_COUNT; ++i) {
            if (fcfs_g_running_processes[i] == nullptr && !fcfs_g_ready_queue.empty()) {
                std::shared_ptr<Process> process = fcfs_g_ready_queue.front();
                fcfs_g_ready_queue.pop_front();
                process->state = ProcessState::RUNNING;
                process->assigned_core = i;
                fcfs_g_running_processes[i] = process;
            }
        }
    }
}

// --- CPU Worker Thread Function ---
// --- MODIFIED: CPU worker now handles memory access, page faults, and segfaults for FCFS ---
void fcfs_core_worker_func(int core_id) {
    while (fcfs_g_is_running) {
        std::shared_ptr<Process> my_process = nullptr;
        {
            std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
            my_process = fcfs_g_running_processes[core_id];
        }

        if (my_process) {
            // --- NEW: Inner loop to run a process until it finishes, blocks, or terminates ---
            while (my_process->program_counter < my_process->commands.size()) {
                const std::string& command_str = my_process->commands[my_process->program_counter];
                
                std::istringstream iss(command_str);
                std::string command_token;
                iss >> command_token;

                if (command_token == "read" || command_token == "write") {
                    int address;
                    iss >> std::hex >> address;
                    bool is_write_op = (command_token == "write");

                    // Core call to the Memory Manager
                    char* physical_ptr = memory_manager->access_memory(*my_process, address, is_write_op);

                    // Outcome 1: Segmentation Fault
                    if (my_process->mem_data.terminated_by_error) {
                        std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
                        std::cout << "\nProcess " << my_process->processName << " terminated: " << my_process->mem_data.termination_reason << std::endl;
                        my_process->state = ProcessState::FINISHED;
                        fcfs_g_finished_processes.push_back(my_process);
                        fcfs_g_running_processes[core_id] = nullptr;
                        memory_manager->deallocate_for_process(*my_process);
                        
                        fcfs_g_scheduler_cv.notify_one();
                        goto next_process_loop; // Break out to get a new process.
                    } 
                    
                    // Outcome 2: Page Fault
                    if (physical_ptr == nullptr) {
                        std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
                        // The MemoryManager set the state to BLOCKED. Move it to the blocked queue.
                        fcfs_g_blocked_queue.push_back(my_process);
                        fcfs_g_running_processes[core_id] = nullptr;
                        fcfs_g_scheduler_cv.notify_one();
                        goto next_process_loop; // Break out to get a new process.
                    }

                    // Outcome 3: Success
                    if (is_write_op) {
                        int value_to_write;
                        iss >> value_to_write;
                        *(reinterpret_cast<uint16_t*>(physical_ptr)) = static_cast<uint16_t>(value_to_write);
                    } else { /* Read logic could be added here */ }

                } else {
                    // This is a non-memory instruction.
                }
                vmstats_increment_active_ticks(); 
                my_process->program_counter++; // Advance to the next instruction on success.
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Your original delay
            }

            // If the while loop finishes naturally, the process has completed all its commands.
            {
                std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
                my_process->state = ProcessState::FINISHED;
                my_process->finish_time = std::chrono::system_clock::now();
                fcfs_g_finished_processes.push_back(my_process);
                fcfs_g_running_processes[core_id] = nullptr;
                memory_manager->deallocate_for_process(*my_process);
                fcfs_g_scheduler_cv.notify_one();
            }
        } else {
            // No process assigned to this core.
            vmstats_increment_idle_ticks();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    next_process_loop:; // The goto label. The loop continues to get the next process.
    }
}

// --- UI Function to Display Process Lists ---
void fcfs_display_processes() {
    std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
    std::cout << "\n-------------------------------------------------------------\n";

    if (fcfs_g_running_processes[0] == nullptr) {
        std::cout << "CPU Utilization: 0%" << std::endl;
    } else {
        std::cout << "CPU Utilization: 100%" << std::endl;
    }

    std::cout << "Running processes:\n";
    for (const auto& p : fcfs_g_running_processes) {
        if (p) {
            std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                      << " (" << fcfs_format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                      << "\tCore: " << p->assigned_core
                      << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }

    std::cout << "\nFinished processes:\n";
    for (const auto& p : fcfs_g_finished_processes) {
        std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                  << " (" << fcfs_format_time(p->finish_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                  << "\tFinished"
                  << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
    }
    std::cout << "-------------------------------------------------------------\n\n";
}

void fcfs_write_processes() {
    std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
    std::ofstream outfile("csopesy-log.txt", std::ios::app); // Open in append mode
    
    if (!outfile.is_open()) {
        std::cerr << "Error: Unable to open file " << "csopesy-log.txt" << " for writing." << std::endl;
        return;
    }

    outfile << "\n-------------------------------------------------------------\n";

    if (fcfs_g_running_processes[0] == nullptr) {
        outfile << "CPU Utilization: 0%" << std::endl;
    } else {
        outfile << "CPU Utilization: 100%" << std::endl;
    }

    outfile << "\nRunning processes:\n";
    for (const auto& p : fcfs_g_running_processes) {
        if (p) {
            outfile << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                  << " (" << fcfs_format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                  << "\tCore: " << p->assigned_core
                  << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }

    outfile << "\nFinished processes:\n";
    for (const auto& p : fcfs_g_finished_processes) {
        outfile << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
              << " (" << fcfs_format_time(p->finish_time, "%m/%d/%Y %I:%M:%S%p") << ")"
              << "\tFinished"
              << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
    }
    outfile << "-------------------------------------------------------------\n\n";
    
    outfile.close();
}


// --- MODIFIED: To use the new Process class and MemoryManager ---
// Note: This function isn't called by the current CLI for FCFS, but is updated for consistency.
void fcfs_create_process(std::string processName, size_t memory_size, MemoryManager& mm) {
    std::shared_ptr<Process> pcb;
    {
        std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
        
        // Use the unified 'Process' class
        pcb = std::make_shared<Process>(cpuClocks++);
        pcb->start_time = std::chrono::system_clock::now();
        pcb->processName = processName;

        // Allocate its memory space in the backing store
        mm.allocate_for_process(*pcb, memory_size);

        // Add some example memory commands
        pcb->commands.push_back("write 0x20 99");
        pcb->commands.push_back("read 0x20");
        
        fcfs_g_ready_queue.push_back(pcb);
    }

    fcfs_g_scheduler_cv.notify_one();

}


// Function that creates processes
// --- MODIFIED: To use MemoryManager and include robust shutdown logic ---
// --- MODIFIED: The function now accepts a MemoryManager reference ---
void fcfs_create_processes(MemoryManager& mm) {
    process_maker_running = true;

    while (process_maker_running) {
        std::shared_ptr<Process> pcb;
        {
            std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
            
            pcb = std::make_shared<Process>(cpuClocks++);
            pcb->start_time = std::chrono::system_clock::now();
            std::stringstream tempString;
            tempString << "process" << pcb->id;
            pcb->processName = tempString.str();

            size_t mem_size = 128;
            // --- MODIFIED: Use the passed-in 'mm' object ---
            mm.allocate_for_process(*pcb, mem_size);

            pcb->commands.push_back("write 0x10 55");
            pcb->commands.push_back("read 0x10");
            
            fcfs_g_ready_queue.push_back(pcb);
        }
        fcfs_g_scheduler_cv.notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Shutdown Logic (remains the same)
    // ...
    // ... (keep your existing shutdown logic here) ...
    // ...
    fcfs_g_is_running = false;
    fcfs_g_scheduler_cv.notify_all();
}

// --- MODIFIED: Main FCFS function to start its threads ---
int FCFS() {
    // Reset the running flag to true each time the scheduler is started.
    fcfs_g_is_running = true;

    // The process generator thread is started by the 'scheduler-start' command in the CLI,
    // so we do NOT start it here.

    // This function's only job is to create the scheduler and worker threads.
    std::thread scheduler(fcfs_scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < CPU_COUNT; ++i) {
        core_workers.emplace_back(fcfs_core_worker_func, i);
    }
    
    // The main FCFS thread (the one launched from the CLI) will now wait here.
    // It will only unblock and return when the threads have finished,
    // which happens after the shutdown signal is sent.
    scheduler.join();
    for (auto& worker : core_workers) {
        worker.join();
    }
    
    return 0;
}