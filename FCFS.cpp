#include <iostream>
#include <vector>
#include <string>
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
#include <algorithm>

// --- Central Headers ---
#include "global.h" // Corrected to 'global.h' as you specified
#include "FCFS.h"
#include "config.h"

// --- File-local variables ---
std::random_device fcfs_rd;
std::mt19937 fcfs_gen(fcfs_rd());

// --- Forward Declarations ---
void fcfs_scheduler_thread_func();
void fcfs_core_worker_func(int core_id);

// --- Helper Function to Format Time ---
std::string fcfs_format_time(const std::chrono::system_clock::time_point& tp, const std::string& fmt) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, fmt.c_str());
    return ss.str();
}

// --- The Scheduler Thread ---
void fcfs_scheduler_thread_func() {
    while (fcfs_g_is_running) {
        std::unique_lock<std::mutex> lock(fcfs_g_process_mutex);

        fcfs_g_scheduler_cv.wait(lock, [&]() {
            if (!fcfs_g_is_running) return true;
            bool core_is_free = std::any_of(fcfs_g_running_processes.begin(), fcfs_g_running_processes.end(), [](const auto& p){ return p == nullptr; });
            // CORRECTION: The scheduler should also wake up for creation requests.
            return !g_creation_queue.empty() || !fcfs_g_blocked_queue.empty() || (core_is_free && !fcfs_g_ready_queue.empty());
        });

        if (!fcfs_g_is_running) break;

        // --- NEW: Handle Process Creation Requests (for consistency with RR) ---
        while (!g_creation_queue.empty()) {
            ProcessCreationRequest request = g_creation_queue.front();
            g_creation_queue.pop_front();
            
            std::shared_ptr<Process> pcb = std::make_shared<Process>(cpuClocks++);
            pcb->start_time = std::chrono::system_clock::now();
            pcb->processName = request.name;
            
            memory_manager->allocate_for_process(*pcb, request.memory_size);

            // Add valid commands
            if (request.memory_size > 2) {
                pcb->commands.push_back("write 0x0 111");
                pcb->commands.push_back("read 0x0");
            }
            fcfs_g_ready_queue.push_back(pcb); 
        }

        // --- Unblock processes ---
        while (!fcfs_g_blocked_queue.empty()) {
            std::shared_ptr<Process> unblocked_process = fcfs_g_blocked_queue.front();
            fcfs_g_blocked_queue.pop_front();
            unblocked_process->state = ProcessState::READY;
            fcfs_g_ready_queue.push_back(unblocked_process);
        }

        // --- Assign ready processes to cores ---
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

// --- The CPU Worker Thread ---
void fcfs_core_worker_func(int core_id) {
    while (fcfs_g_is_running) {
        std::shared_ptr<Process> my_process = nullptr;
        {
            std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
            my_process = fcfs_g_running_processes[core_id];
        }

        if (my_process) {
            while (my_process->program_counter < my_process->commands.size()) {
                const std::string& command_str = my_process->commands[my_process->program_counter];
                std::istringstream iss(command_str);
                std::string command_token;
                iss >> command_token;

                if (command_token == "read" || command_token == "write") {
                    int address;
                    iss >> std::hex >> address;
                    bool is_write_op = (command_token == "write");
                    char* physical_ptr = memory_manager->access_memory(*my_process, address, is_write_op);

                    if (my_process->mem_data.terminated_by_error) {
                        {
                           std::lock_guard<std::mutex> lock(g_cout_mutex);
                           std::cout << "\nProcess " << my_process->processName << " terminated: " << my_process->mem_data.termination_reason << std::endl;
                        }
                        {
                            std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
                            my_process->state = ProcessState::FINISHED;
                            fcfs_g_finished_processes.push_back(my_process);
                            fcfs_g_running_processes[core_id] = nullptr;
                            memory_manager->deallocate_for_process(*my_process);
                            fcfs_g_scheduler_cv.notify_one();
                        }
                        goto next_process_loop;
                    } 
                    if (physical_ptr == nullptr) {
                        std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
                        my_process->state = ProcessState::BLOCKED;
                        fcfs_g_blocked_queue.push_back(my_process);
                        fcfs_g_running_processes[core_id] = nullptr;
                        fcfs_g_scheduler_cv.notify_one();
                        goto next_process_loop;
                    }
                    if (is_write_op) {
                        int value_to_write;
                        iss >> value_to_write;
                        *(reinterpret_cast<uint16_t*>(physical_ptr)) = static_cast<uint16_t>(value_to_write);
                    }
                }
                my_process->program_counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

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
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    next_process_loop:;
    }
}

// --- UI Functions ---
void fcfs_display_processes() {
    // Lock the mutex for a consistent snapshot.
    std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
    
    // --- NEW: Calculate CPU Utilization ---
    int busyCores = 0;
    for (const auto& p : fcfs_g_running_processes) {
        if (p != nullptr) {
            busyCores++;
        }
    }
    int cpuUtil = CPU_COUNT > 0 ? static_cast<int>(100.0 * busyCores / CPU_COUNT) : 0;

    // --- Display the output ---
    std::cout << "\n-------------------------------------------------------------\n";
    
    // Print the new CPU utilization line.
    std::cout << "CPU Utilization: " << cpuUtil << "%" << std::endl;

    // The rest of your original display logic remains.
    std::cout << "\nRunning processes:\n";
    for (const auto& p : fcfs_g_running_processes) {
        if (p) {
            // Updated to use your original, more detailed format
            std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                      << " (" << fcfs_format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                      << "\tCore: " << p->assigned_core
                      << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }

    std::cout << "\nFinished processes:\n";
    for (const auto& p : fcfs_g_finished_processes) {
        // Updated to use your original, more detailed format
        std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                  << " (" << fcfs_format_time(p->finish_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                  << "\tFinished"
                  << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
    }
    std::cout << "-------------------------------------------------------------\n\n";
}

void fcfs_write_processes() {
    // Lock the mutex to get a consistent snapshot of the process lists.
    std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
    
    // Open the log file in append mode.
    std::ofstream outfile("csopesy-log.txt", std::ios::app); 
    
    if (!outfile.is_open()) {
        // Use cerr for errors, as it's unbuffered.
        std::cerr << "Error: Unable to open file csopesy-log.txt for writing." << std::endl;
        return;
    }

    outfile << "\n--------------------- FCFS REPORT ---------------------\n";

    // --- NEW: Calculate CPU Utilization (same as the display function) ---
    int busyCores = 0;
    for (const auto& p : fcfs_g_running_processes) {
        if (p != nullptr) {
            busyCores++;
        }
    }
    int cpuUtil = CPU_COUNT > 0 ? static_cast<int>(100.0 * busyCores / CPU_COUNT) : 0;
    outfile << "CPU Utilization: " << cpuUtil << "%" << std::endl;

    // --- Print the lists (using your original detailed format) ---
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
        if (p) {
            outfile << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                    << " (" << fcfs_format_time(p->finish_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                    << "\tFinished"
                    << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }
    outfile << "-------------------------------------------------------------\n\n";
    
    outfile.close();
}

// --- The Process Generator for 'scheduler-start' ---
// CORRECTION: This now adds requests to the shared queue.
void fcfs_create_processes(MemoryManager& mm) {
    process_maker_running = true;
    while (process_maker_running) {
        {
            // Lock the appropriate mutex. FCFS should use its own, but we can share for simplicity.
            std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
            g_creation_queue.push_back({"fcfs_proc" + std::to_string(cpuClocks), 128});
        }
        fcfs_g_scheduler_cv.notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(processFrequency));
    }

    // --- Shutdown Logic ---
    while (true) {
        bool all_done;
        {
            std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
            bool running_is_empty = std::all_of(fcfs_g_running_processes.begin(), fcfs_g_running_processes.end(), [](const auto& p){ return p == nullptr; });
            all_done = g_creation_queue.empty() && fcfs_g_ready_queue.empty() && fcfs_g_blocked_queue.empty() && running_is_empty;
        }
        if (all_done) break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    fcfs_g_is_running = false;
    fcfs_g_scheduler_cv.notify_all();
}

// --- The Main Scheduler Entry Point ---
int FCFS() {
    fcfs_g_is_running = true;
    
    std::thread scheduler(fcfs_scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < CPU_COUNT; ++i) {
        core_workers.emplace_back(fcfs_core_worker_func, i);
    }
    
    scheduler.join();
    for (auto& worker : core_workers) {
        worker.join();
    }
    
    return 0;
}