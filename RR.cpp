#include <iostream>
#include <ostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <random>
#include <map> 
#include <algorithm>
#include <thread>

// --- Central Headers ---
#include "global.h" 
#include "RR.h"      
#include "config.h"  
#include "vmstat.h"  

// --- File-local variables ---
int memoryCycle = 0;
std::random_device rr_rd;
std::mt19937 rr_gen(rr_rd());

// --- Forward Declarations for functions defined in this file ---
void rr_scheduler_thread_func();
void rr_core_worker_func(int core_id);

// --- Helper Function to Format Time ---
std::string rr_format_time(const std::chrono::system_clock::time_point& tp, const std::string& fmt) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, fmt.c_str());
    return ss.str();
} 

// --- Helper Function to Get Process Names ---
std::vector<std::string> rr_getRunningProcessNames() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex); 
    std::vector<std::string> out; 
    for (const auto& p : rr_g_running_processes) {
        if (p) out.emplace_back(p->processName);
    } 
    return out;
}

// --- Helper for Memory Logging ---
void display_memory() {
    std::ostringstream filename;
    filename << "memory_stamp_" << memoryCycle++ << ".txt";
    std::ofstream memory_file(filename.str());
    auto frame_snapshot = memory_manager->get_frame_snapshot();
    int occupied_frames = 0;
    for(const auto& frame : frame_snapshot) {
        if (!frame.is_free) { occupied_frames++; }
    }
    memory_file << "Timestamp: (" << rr_format_time(std::chrono::system_clock::now(), "%m/%d/%Y %I:%M:%S%p") << ")" << std::endl;
    memory_file << "Total Frames: " << frame_snapshot.size() << std::endl;
    memory_file << "Occupied Frames: " << occupied_frames << std::endl;
    memory_file << "Unused Frames: " << frame_snapshot.size() - occupied_frames << std::endl << std::endl;
    memory_file << "---- Memory Layout (Top to Bottom) ----" << std::endl;
    memory_file << "----end---- = " << MAX_OVERALL_MEM << std::endl;
    std::map<int, std::string> pid_to_name_map;
    {
        std::lock_guard<std::mutex> lock(rr_g_process_mutex);
        auto populate_map = [&](const auto& process_list){
            for(const auto& p : process_list) { if(p) pid_to_name_map[p->id] = p->processName; }
        };
        populate_map(rr_g_ready_queue);
        populate_map(rr_g_running_processes);
        populate_map(rr_g_blocked_queue);
        populate_map(rr_g_finished_processes);
    }
    for (int i = frame_snapshot.size() - 1; i >= 0; --i) {
        const auto& frame = frame_snapshot[i];
        long long upper_bound = (long long)(i + 1) * MEM_PER_FRAME;
        long long lower_bound = (long long)i * MEM_PER_FRAME;
        memory_file << upper_bound << std::endl;
        if (frame.is_free) { memory_file << "[  FREE  ]" << std::endl; } 
        else {
            std::string name = "PID " + std::to_string(frame.owner_pid);
            if(pid_to_name_map.count(frame.owner_pid)) { name = pid_to_name_map[frame.owner_pid]; }
            memory_file << name << std::endl;
        }
        memory_file << lower_bound << std::endl << std::endl;
    }
    memory_file << "----start---- = 0" << std::endl;
    memory_file.close();
}

// --- The Scheduler Thread ---
void rr_scheduler_thread_func() {
    while (rr_g_is_running) {
        std::unique_lock<std::mutex> lock(rr_g_process_mutex);

        rr_g_scheduler_cv.wait(lock, [&]() {
            if (!rr_g_is_running) return true;
            bool core_is_free = std::any_of(rr_g_running_processes.begin(), rr_g_running_processes.end(), [](const auto& p){ return p == nullptr; });
            return !g_creation_queue.empty() || !rr_g_blocked_queue.empty() || (core_is_free && !rr_g_ready_queue.empty());
        });

        if (!rr_g_is_running) break;

        // Priority 1: Handle process creation requests from the CLI
        while (!g_creation_queue.empty()) {
            ProcessCreationRequest request = g_creation_queue.front();
            g_creation_queue.pop_front();
            
            std::shared_ptr<Process> pcb = std::make_shared<Process>(cpuClocks++);
            pcb->start_time = std::chrono::system_clock::now();
            pcb->processName = request.name;
            
            memory_manager->allocate_for_process(*pcb, request.memory_size);

            if (request.memory_size > 2) {
                // Give processes a heavy workload to force preemption
                for (int i = 0; i < 50; ++i) { 
                    pcb->commands.push_back("write 0x0 123"); 
                    pcb->commands.push_back("read 0x0");
                }
            }

            rr_g_ready_queue.push_back(pcb); 
        }

        // Priority 2: Unblock processes
        while (!rr_g_blocked_queue.empty()) {
            std::shared_ptr<Process> unblocked_process = rr_g_blocked_queue.front();
            rr_g_blocked_queue.pop_front();
            unblocked_process->state = ProcessState::READY;
            rr_g_ready_queue.push_back(unblocked_process);
        }

        // Priority 3: Assign ready processes to cores
        for (int i = 0; i < CPU_COUNT; ++i) {
            if (rr_g_running_processes[i] == nullptr && !rr_g_ready_queue.empty()) {
                std::shared_ptr<Process> process = rr_g_ready_queue.front();
                rr_g_ready_queue.pop_front();
                process->state = ProcessState::RUNNING;
                process->assigned_core = i;
                process->commands_executed_this_quantum = 0;
                rr_g_running_processes[i] = process;
            }
        }
    }
}

// --- The CPU Worker Thread (Correct, Deadlock-Free Version) ---
// --- FINAL, CORRECT, DEADLOCK-FREE WORKER THREAD ---
void rr_core_worker_func(int core_id) {
    while (rr_g_is_running) {
        std::shared_ptr<Process> my_process;

        // Step 1: Briefly lock ONLY to get a pointer to the process.
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            my_process = rr_g_running_processes[core_id];
        }

        // If no process, idle and loop again.
        if (!my_process) {
            vmstats_increment_idle_ticks();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Step 2: Failsafe check for already finished process.
        if (my_process->program_counter >= my_process->commands.size()) {
            {
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                rr_g_finished_processes.push_back(my_process);
                rr_g_running_processes[core_id] = nullptr;
                memory_manager->deallocate_for_process(*my_process);
                rr_g_scheduler_cv.notify_one();
            }
            continue;
        }

        // Step 3: Execute one instruction. This happens OUTSIDE the main lock.
        const std::string& command_str = my_process->commands[my_process->program_counter];
        std::istringstream iss(command_str);
        std::string command_token;
        iss >> command_token;
        bool success = true;

        if (command_token == "read" || command_token == "write") {
            int address;
            iss >> std::hex >> address;
            bool is_write = (command_token == "write");
            
            // Memory access is a slow operation, so it's critical it happens without holding the process lock.
            char* physical_ptr = memory_manager->access_memory(*my_process, address, is_write);

            if (my_process->mem_data.terminated_by_error) {
                success = false;
                // Lock cout, print, then lock process list to terminate.
                { std::lock_guard<std::mutex> lock(g_cout_mutex); std::cout << "\nProcess " << my_process->processName << " terminated: " << my_process->mem_data.termination_reason << std::endl; }
                {
                    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                    my_process->state = ProcessState::FINISHED;
                    rr_g_finished_processes.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    memory_manager->deallocate_for_process(*my_process);
                    rr_g_scheduler_cv.notify_one();
                }
            } else if (physical_ptr == nullptr) {
                success = false;
                // Lock process list to block the process.
                {
                    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                    my_process->state = ProcessState::BLOCKED;
                    rr_g_blocked_queue.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    rr_g_scheduler_cv.notify_one();
                }
            } else {
                if (is_write) {
                    int value; iss >> value;
                    *(reinterpret_cast<uint16_t*>(physical_ptr)) = static_cast<uint16_t>(value);
                }
                vmstats_increment_active_ticks();
            }
        } else {
            vmstats_increment_active_ticks();
        }

        // Step 4: If instruction was successful, update counters and check for end-of-quantum or completion.
        if (success) {
            my_process->program_counter++;
            my_process->commands_executed_this_quantum++;

            bool is_finished = (my_process->program_counter >= my_process->commands.size());
            bool quantum_expired = (my_process->commands_executed_this_quantum >= qCycles);

            if (is_finished || quantum_expired) {
                // Now, briefly lock ONLY to update the process lists.
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                if (is_finished) {
                    my_process->state = ProcessState::FINISHED;
                    my_process->finish_time = std::chrono::system_clock::now();
                    rr_g_finished_processes.push_back(my_process);
                    memory_manager->deallocate_for_process(*my_process);
                } else { // Quantum expired
                    my_process->state = ProcessState::READY;
                    rr_g_ready_queue.push_back(my_process);
                }
                rr_g_running_processes[core_id] = nullptr;
                rr_g_scheduler_cv.notify_one();
            }
        }
    }
}

// --- UI Functions ---
void rr_display_processes() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
    std::cout << "\n-------------------------------------------------------------\n";
    std::cout << "\nRunning processes:\n";
    for (const auto& p : rr_g_running_processes) {
        if (p) {
            std::cout << "  " << p->processName << " (ID: " << p->id << ")\n";
        }
    }
    std::cout << "\nFinished processes:\n";
    for (const auto& p : rr_g_finished_processes) {
        if (p) {
            std::cout << "  " << p->processName << " (ID: " << p->id << ")\n";
        }
    }
    std::cout << "-------------------------------------------------------------\n\n";
}

void rr_create_process_with_commands(std::string processName, size_t memory_size, const std::vector<std::string>& commands) {
    // Create a new process
    std::shared_ptr<Process> pcb;
    
    pcb = std::make_shared<Process>(cpuClocks);
    pcb->start_time = std::chrono::system_clock::now(); 
    pcb->processName = processName;
    pcb->memory_size = memory_size;

    pcb->commands = commands;

    cpuClocks++;
    rr_g_ready_queue.push_back(pcb);

    // Notify scheduler that a new process is available
    rr_g_scheduler_cv.notify_one();
}

void rr_write_processes() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
    std::ofstream outfile("csopesy-log.txt", std::ios::app);
    outfile << "--- RR SCHEDULER REPORT ---\n";
    outfile << "Running processes:\n";
    for (const auto& p : rr_g_running_processes) {
        if (p) outfile << "  " << p->processName << "\n";
    }
    outfile << "Finished processes:\n";
    for (const auto& p : rr_g_finished_processes) {
        if (p) outfile << "  " << p->processName << "\n";
    }
    outfile.close();
}

// --- The Process Generator for 'scheduler-start' ---
void rr_create_processes(MemoryManager& mm) {
    process_maker_running = true;
    while (process_maker_running) {
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            g_creation_queue.push_back({"process" + std::to_string(cpuClocks++), 256});
        }
        rr_g_scheduler_cv.notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(processFrequency));
    }

    // --- Shutdown Logic ---
    while (true) {
        bool all_done;
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            bool running_is_empty = std::all_of(rr_g_running_processes.begin(), rr_g_running_processes.end(), [](const auto& p){ return p == nullptr; });
            all_done = g_creation_queue.empty() && rr_g_ready_queue.empty() && rr_g_blocked_queue.empty() && running_is_empty;
        }
        if (all_done) break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    rr_g_is_running = false;
    rr_g_scheduler_cv.notify_all();
}

// --- The Main Scheduler Entry Point ---
int RR() {
    rr_g_is_running = true;
    
    std::thread scheduler(rr_scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < CPU_COUNT; ++i) {
        core_workers.emplace_back(rr_core_worker_func, i);
    }
    
    scheduler.join();
    for (auto& worker : core_workers) {
        worker.join();
    }
    
    return 0;
}