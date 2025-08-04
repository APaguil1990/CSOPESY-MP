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
#include "global.h" // Provides all global variables and class definitions
#include "RR.h"      // Provides our function declarations
#include "config.h"  // Provides config variables not in globals
#include "vmstat.h"  // Provides vmstat functions

// --- File-local variables ---
int memoryCycle = 0;
std::random_device rr_rd;
std::mt19937 rr_gen(rr_rd());

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
        if (!frame.is_free) {
            occupied_frames++;
        }
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
            for(const auto& p : process_list) {
                if(p) pid_to_name_map[p->id] = p->processName;
            }
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
        if (frame.is_free) {
            memory_file << "[  FREE  ]" << std::endl;
        } else {
            std::string name = "PID " + std::to_string(frame.owner_pid);
            if(pid_to_name_map.count(frame.owner_pid)) {
                name = pid_to_name_map[frame.owner_pid];
            }
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

             pcb->commands.push_back("write 0x0 123"); 
            
            // This will read the value back.
            pcb->commands.push_back("read 0x0");

            // This calculates an address in the middle of the process's allocated memory,
            // which is guaranteed to be a valid address.
            if (request.memory_size > 16) { // Make sure there's enough space
                int middle_address = request.memory_size / 2;
                
                // We use a stringstream to build the command string with the calculated hex address.
                std::stringstream ss;
                ss << "write 0x" << std::hex << middle_address << " 456";
                pcb->commands.push_back(ss.str());

                // Clear the stringstream to reuse it for the read command.
                ss.str("");
                ss.clear();
                ss << "read 0x" << std::hex << middle_address;
                pcb->commands.push_back(ss.str());

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

// --- The CPU Worker Thread ---
void rr_core_worker_func(int core_id) {
    while (rr_g_is_running) {
        std::shared_ptr<Process> my_process = nullptr;
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            my_process = rr_g_running_processes[core_id];
        }

        if (my_process) {
            if (my_process->program_counter >= my_process->commands.size()) {
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                my_process->state = ProcessState::FINISHED;
                my_process->finish_time = std::chrono::system_clock::now();
                rr_g_finished_processes.push_back(my_process);
                rr_g_running_processes[core_id] = nullptr;
                memory_manager->deallocate_for_process(*my_process);
                rr_g_scheduler_cv.notify_one();
                continue;
            }

            const std::string& command_str = my_process->commands[my_process->program_counter];
            bool instruction_succeeded = true;

            std::istringstream iss(command_str);
            std::string command_token;
            iss >> command_token;
            
            if (command_token == "read" || command_token == "write") {
                int address;
                iss >> std::hex >> address; 
                bool is_write_op = (command_token == "write");
                
                char* physical_ptr = memory_manager->access_memory(*my_process, address, is_write_op);

                if (my_process->mem_data.terminated_by_error) {
                    instruction_succeeded = false;
                    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                    std::cout << "\nProcess " << my_process->processName << " terminated: " << my_process->mem_data.termination_reason << std::endl;
                    my_process->state = ProcessState::FINISHED;
                    rr_g_finished_processes.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    memory_manager->deallocate_for_process(*my_process);
                    rr_g_scheduler_cv.notify_one();
                } else if (physical_ptr == nullptr) {
                    instruction_succeeded = false;
                    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                    my_process->state = ProcessState::BLOCKED; // Set state to BLOCKED
                    rr_g_blocked_queue.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    rr_g_scheduler_cv.notify_one();
                } else {
                    if (is_write_op) {
                        int value_to_write;
                        iss >> value_to_write;
                        *(reinterpret_cast<uint16_t*>(physical_ptr)) = static_cast<uint16_t>(value_to_write);
                    } else { 
                        uint16_t value_read = *(reinterpret_cast<uint16_t*>(physical_ptr));
                    }
                    vmstats_increment_active_ticks();
                }
            } else {
                vmstats_increment_active_ticks();
            }

            if (instruction_succeeded) {
                my_process->program_counter++;
                my_process->commands_executed_this_quantum++;
                
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);

                if (my_process->program_counter >= my_process->commands.size()) {
                    my_process->state = ProcessState::FINISHED;
                    my_process->finish_time = std::chrono::system_clock::now();
                    rr_g_finished_processes.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    memory_manager->deallocate_for_process(*my_process);
                    rr_g_scheduler_cv.notify_one();
                } else if (my_process->commands_executed_this_quantum >= qCycles) {
                    my_process->state = ProcessState::READY;
                    rr_g_ready_queue.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    rr_g_scheduler_cv.notify_one();
                }
            }
        } else {
            vmstats_increment_idle_ticks();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}


// --- UI Functions (These should be compatible, but kept for your reference) ---
void rr_display_processes() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
    std::cout << "\n-------------------------------------------------------------\n";
    // ... [Your original display logic here] ...
    std::cout << "\nRunning processes:\n";
    for (const auto& p : rr_g_running_processes) {
        if (p) {
            std::cout << "process" << p->id << " (" << p->processName << ")\n";
        }
    }
    std::cout << "\nFinished processes:\n";
    for (const auto& p : rr_g_finished_processes) {
        if (p) {
            std::cout << "process" << p->id << " (" << p->processName << ")\n";
        }
    }
    std::cout << "-------------------------------------------------------------\n\n";
}

void rr_write_processes() {
    // ... [Your original write logic here] ...
}

// --- The Process Generator for 'scheduler-start' ---
void rr_create_processes(MemoryManager& mm) {
    process_maker_running = true;
    while (process_maker_running) {
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            // Add a request to the creation queue. The scheduler will handle the rest.
            g_creation_queue.push_back({"process" + std::to_string(cpuClocks), 256});
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