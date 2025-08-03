#include <iostream>
#include <ostream>
#include <vector>
#include <string>
#include <map> 
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
#include <unordered_set>

#include "Process.h"
#include "MemoryManager.h"
#include "ScreenManager.h"
#include "config.h"
#include "vmstat.h"

std::mutex g_cout_mutex;

int memoryCycle = 0;



std::random_device rr_rd;  // a seed source for the random number engine
std::mt19937 rr_gen(rr_rd()); // mersenne_twister_engine seeded with rd()
extern std::deque<std::shared_ptr<Process>> rr_g_ready_queue;
extern std::vector<std::shared_ptr<Process>> rr_g_running_processes;
extern std::vector<std::shared_ptr<Process>> rr_g_finished_processes;
extern std::deque<std::shared_ptr<Process>> rr_g_blocked_queue; // New queue for page faults

extern std::mutex rr_g_process_mutex;
extern std::condition_variable rr_g_scheduler_cv;
extern std::atomic<bool> rr_g_is_running;
extern MemoryManager memory_manager;
// --- Helper Function to Format Time ---
std::string rr_format_time(const std::chrono::system_clock::time_point& tp, const std::string& fmt) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, fmt.c_str());
    return ss.str();
} 

std::vector<std::string> rr_getRunningProcessNames() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex); 
    std::vector<std::string> out; 

    for (const auto& p : rr_g_running_processes) {
        if (p) out.emplace_back(p->processName);
    } 
    return out;
}

void rr_declareCommand() {
    std::uniform_int_distribution<> declare_rand(0, 2);

    int declared = declare_rand(rr_gen);

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

// --- FINAL, CORRECTED VERSION for a Paged System ---
void display_memory() {
    std::ostringstream filename;
    filename << "memory_stamp_" << memoryCycle++ << ".txt";
    std::ofstream memory_file(filename.str());

    // --- Get a snapshot of the current state of all memory frames ---
    auto frame_snapshot = memory_manager.get_frame_snapshot();
    
    int occupied_frames = 0;
    for(const auto& frame : frame_snapshot) {
        if (!frame.is_free) {
            occupied_frames++;
        }
    }

    // --- Write Header Information (Reflecting a Paged System) ---
    memory_file << "Timestamp: (" << rr_format_time(std::chrono::system_clock::now(), "%m/%d/%Y %I:%M:%S%p") << ")" << std::endl;
    memory_file << "Total Frames: " << frame_snapshot.size() << std::endl;
    memory_file << "Occupied Frames: " << occupied_frames << std::endl;
    // With paging, "external fragmentation" doesn't exist. The unused space is "internal fragmentation" within the last page of each process,
    // or simply unused frames. We will report unused frames.
    memory_file << "Unused Frames: " << frame_snapshot.size() - occupied_frames << std::endl << std::endl;

    // --- Write the Visual Memory Layout (Frame by Frame) ---
    memory_file << "---- Memory Layout (Top to Bottom) ----" << std::endl;
    memory_file << "----end---- = " << MAX_OVERALL_MEM << std::endl;

    // Create a map to get process names from PIDs. This is more efficient than searching lists repeatedly.
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
        populate_map(rr_g_finished_processes); // Include finished in case they were just evicted
    }
    
    // Iterate through the frames from last to first to match your "top of memory" style.
    for (int i = frame_snapshot.size() - 1; i >= 0; --i) {
        const auto& frame = frame_snapshot[i];
        long long upper_bound = (long long)(i + 1) * MEM_PER_FRAME;
        long long lower_bound = (long long)i * MEM_PER_FRAME;

        memory_file << upper_bound << std::endl;
        if (frame.is_free) {
            memory_file << "[  FREE  ]" << std::endl;
        } else {
            // Find the process name from the map
            std::string name = "PID " + std::to_string(frame.owner_pid); // Fallback name
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

void rr_addCommand() {
    variable_a = variable_b + variable_c;
}

void rr_subtractCommand() {
    variable_a = variable_b - variable_c;
}

void rr_sleepCommand() {
    // cpuClocks += 10;
}

void rr_forCommand() {
    for (int i; i < 5; i++) {
        rr_addCommand();
    }
}

// --- Scheduler Thread Function ---
// --- MODIFIED: Scheduler thread now unblocks processes and simplifies scheduling ---
void rr_scheduler_thread_func() {
    while (rr_g_is_running) {
        std::unique_lock<std::mutex> lock(rr_g_process_mutex);

        // --- NEW: Unblock processes that finished I/O ---
        // This is the first thing the scheduler does in its cycle. It simulates
        // the OS responding to I/O completion interrupts from the disk controller.
        while (!rr_g_blocked_queue.empty()) {
            std::shared_ptr<Process> unblocked_process = rr_g_blocked_queue.front();
            rr_g_blocked_queue.pop_front();
            unblocked_process->state = ProcessState::READY; // Change state back to READY
            rr_g_ready_queue.push_back(unblocked_process);
        }

        // --- ORIGINAL LOGIC (Unchanged) ---
        // The condition to wake up is the same: a core is free and a process is ready.
        rr_g_scheduler_cv.wait(lock, [&]() {
            if (!rr_g_is_running) return true;
            bool core_is_free = false;
            for (const auto& p : rr_g_running_processes) {
                if (p == nullptr) {
                    core_is_free = true;
                    break;
                }
            }
            return core_is_free && !rr_g_ready_queue.empty();
        });

        if (!rr_g_is_running) break;

        // --- SIMPLIFIED SCHEDULING LOGIC ---
        // The old `if (rr_g_memory_processes.size() < FRAME_COUNT ...)` check is removed.
        // We now directly schedule any ready process.
        for (int i = 0; i < CPU_COUNT; ++i) {
            if (rr_g_running_processes[i] == nullptr && !rr_g_ready_queue.empty()) {
                // MODIFIED: Use the new unified 'Process' class, not RR_PCB.
                std::shared_ptr<Process> process = rr_g_ready_queue.front();
                rr_g_ready_queue.pop_front();
                
                process->state = ProcessState::RUNNING;
                process->assigned_core = i;
                process->commands_executed_this_quantum = 0; // Reset quantum count
                rr_g_running_processes[i] = process;
            }
        }
    }
}



// --- CPU Worker Thread Function ---
// --- MODIFIED: CPU worker now handles memory access, page faults, and segfaults ---
void rr_core_worker_func(int core_id) {
    while (rr_g_is_running) {
        std::shared_ptr<Process> my_process = nullptr;
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            my_process = rr_g_running_processes[core_id];
        }

        if (my_process) {
            // Pre-check to ensure the process still has commands to run.
            if (my_process->program_counter >= my_process->commands.size()) {
                // This is a failsafe. This logic is also handled below, but it's good practice.
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                my_process->state = ProcessState::FINISHED;
                my_process->finish_time = std::chrono::system_clock::now();
                rr_g_finished_processes.push_back(my_process);
                rr_g_running_processes[core_id] = nullptr;
                memory_manager.deallocate_for_process(*my_process); // Release memory frames
                rr_g_scheduler_cv.notify_one();
                continue; // Go to next worker loop iteration
            }

            const std::string& command_str = my_process->commands[my_process->program_counter];
            bool instruction_succeeded = true; // Assume success until a fault occurs.

            // --- NEW: Parse command and handle memory access ---
            std::istringstream iss(command_str);
            std::string command_token;
            iss >> command_token;
            
            // We define "read" and "write" as our memory-accessing instructions.
            if (command_token == "read" || command_token == "write") {
                int address;
                // Read the address from the command string (e.g., "read 0x100")
                iss >> std::hex >> address; 
                bool is_write_op = (command_token == "write");
                
                // This is the core call to the memory management unit.
                char* physical_ptr = memory_manager.access_memory(*my_process, address, is_write_op);

                // --- NEW: Handle the 3 possible outcomes ---

                // Outcome 1: Segmentation Fault (Access Violation)
                if (my_process->mem_data.terminated_by_error) {
                    instruction_succeeded = false; // The instruction failed catastrophically.
                    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                    std::cout << "\nProcess " << my_process->processName << " terminated: " << my_process->mem_data.termination_reason << std::endl;
                    my_process->state = ProcessState::FINISHED;
                    rr_g_finished_processes.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    memory_manager.deallocate_for_process(*my_process);
                    rr_g_scheduler_cv.notify_one(); // Notify scheduler of the open core.
                
                // Outcome 2: Page Fault (Valid address, but not in a frame)
                } else if (physical_ptr == nullptr) {
                    instruction_succeeded = false; // The instruction failed and must be retried.
                    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                    // The MemoryManager has already set the process state to BLOCKED.
                    // We move the process to the blocked queue for the scheduler to handle.
                    rr_g_blocked_queue.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    rr_g_scheduler_cv.notify_one(); // Notify scheduler of the open core.

                // Outcome 3: Success!
                } else {
                    // The memory access was successful, physical_ptr points to the correct location in RAM.
                    if (is_write_op) {
                        int value_to_write;
                        iss >> value_to_write;
                        // Write a 2-byte (uint16_t) value to the physical address.
                        *(reinterpret_cast<uint16_t*>(physical_ptr)) = static_cast<uint16_t>(value_to_write);
                    } else { // It's a read operation.
                        uint16_t value_read = *(reinterpret_cast<uint16_t*>(physical_ptr));
                        // In a real system, you'd store this value in a process register or variable.
                        // For now, we can just log it.
                    }
                    vmstats_increment_active_ticks();
                }
            } else {
                // This is a non-memory instruction, like your original 'declare', 'add', etc.
                // It always succeeds and just consumes a tick.
                vmstats_increment_active_ticks();
            }

            // --- Post-Execution Logic ---
            // This part only runs if the instruction did not cause a fault or termination.
            if (instruction_succeeded) {
                my_process->program_counter++; // CRITICAL: Only advance PC on success.
                my_process->commands_executed_this_quantum++;
                
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);

                // Check if the process has finished all its commands.
                if (my_process->program_counter >= my_process->commands.size()) {
                    my_process->state = ProcessState::FINISHED;
                    my_process->finish_time = std::chrono::system_clock::now();
                    rr_g_finished_processes.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    memory_manager.deallocate_for_process(*my_process);
                    rr_g_scheduler_cv.notify_one();

                // Check if the process's time slice (quantum) has ended.
                } else if (my_process->commands_executed_this_quantum >= qCycles) {
                    my_process->state = ProcessState::READY;
                    rr_g_ready_queue.push_back(my_process); // Return to the back of the ready queue.
                    rr_g_running_processes[core_id] = nullptr;
                    rr_g_scheduler_cv.notify_one();
                }
            }
            
        } else {
            // No process assigned to this core, it remains idle.
            vmstats_increment_idle_ticks();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// find process for screen -r (log file in memory)
// void rr_search_process(std::string process_search) {
//     std::lock_guard<std::mutex> lock(rr_g_process_mutex);
//     std::cout << "\n-------------------------------------------------------------\n";

//     for (const auto& p : rr_g_ready_queue) {
//         std::stringstream tempString;
//         tempString << "process" << p->id;

//         if (process_search.compare(p->processName) == 0 || process_search.compare(tempString.str()) == 0) {
//             for(const std::string& line : p->log_file) {
//                 std::cout << line << std::endl;
//             }
//         }
//     }
//     std::cout << "-------------------------------------------------------------\n\n";
//     std::cout << process_search;
// }

// --- UI Function to Display Process Lists ---
void rr_display_processes() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
    std::cout << "\n-------------------------------------------------------------\n";

    if (rr_g_running_processes[0] == nullptr) {
        std::cout << "CPU Utilization: 0%" << std::endl;
    } else {
        std::cout << "CPU Utilization: 100%" << std::endl;
    }

    // std::cout << "\nMemory processes:\n";
    // for (const auto& p : rr_g_memory_processes) {
    //     if (p) {
    //         std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
    //                   << " (" << rr_format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
    //                   << "\tCore: " << p->assigned_core
    //                   << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
    //     }
    // }
    
    // std::cout << "\nReady processes:\n";
    // for (const auto& p : rr_g_ready_queue) {
    //     if (p) {
    //         std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
    //                   << " (" << rr_format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
    //                   << "\tCore: " << p->assigned_core
    //                   << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
    //     }
    // }

    std::cout << "\nRunning processes:\n";
    for (const auto& p : rr_g_running_processes) {
        if (p) {
            std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                      << " (" << rr_format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                      << "\tCore: " << p->assigned_core
                      << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }

    std::cout << "\nFinished processes:\n";
    for (const auto& p : rr_g_finished_processes) {
        std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                  << " (" << rr_format_time(p->finish_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                  << "\tFinished"
                  << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
    }
    std::cout << "-------------------------------------------------------------\n\n";
}

void rr_write_processes() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
    std::ofstream outfile("csopesy-log.txt", std::ios::app); // Open in append mode
    
    if (!outfile.is_open()) {
        std::cerr << "Error: Unable to open file " << "csopesy-log.txt" << " for writing." << std::endl;
        return;
    }

    outfile << "\n-------------------------------------------------------------\n";

    if (rr_g_running_processes[0] == nullptr) {
        outfile << "CPU Utilization: 0%" << std::endl;
    } else {
        outfile << "CPU Utilization: 100%" << std::endl;
    }

    outfile << "\nRunning processes:\n";
    for (const auto& p : rr_g_running_processes) {
        if (p) {
            outfile << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                  << " (" << rr_format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                  << "\tCore: " << p->assigned_core
                  << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }

    outfile << "\nFinished processes:\n";
    for (const auto& p : rr_g_finished_processes) {
        outfile << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
              << " (" << rr_format_time(p->finish_time, "%m/%d/%Y %I:%M:%S%p") << ")"
              << "\tFinished"
              << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
    }
    outfile << "-------------------------------------------------------------\n\n";
    
    outfile.close();
}

// --- MODIFIED: Updated to create a 'Process' and use the MemoryManager ---
// Note the new function signature to accept a MemoryManager reference.
void rr_create_process(std::string processName, std::size_t memory_size, MemoryManager& mm) {
    std::shared_ptr<Process> pcb;
    {
        std::lock_guard<std::mutex> lock(rr_g_process_mutex);
        
        // MODIFIED: Use the unified 'Process' class.
        pcb = std::make_shared<Process>(cpuClocks++);
        pcb->start_time = std::chrono::system_clock::now();
        pcb->processName = processName;

        // --- NEW: Allocate space via MemoryManager ---
        // This sets up the process's page table and reserves space in the backing store file.
        mm.allocate_for_process(*pcb, memory_size);

        // --- NEW: Add example memory instructions to test the system ---
        // This fulfills the spec requirement for user-defined READ/WRITE instructions.
        pcb->commands.push_back("write 0x0 42");    // Write value 42 to address 0x0
        pcb->commands.push_back("read 0x0");        // Read from address 0x0
        pcb->commands.push_back("write 0x100 101"); // Write value 101 to address 0x100 (256)
        pcb->commands.push_back("read 0x100");      // Read from address 0x100
        
        // This instruction will cause a segmentation fault if memory_size is <= 0xFFFF
        pcb->commands.push_back("read 0xFFFF");     // Attempt to read from an out-of-bounds address.

        rr_g_ready_queue.push_back(pcb);
    }
    
    // Notify the scheduler that a new process is ready.
    rr_g_scheduler_cv.notify_one();
}

// Function that creates processes
// --- MODIFIED: Updated to use MemoryManager and includes robust shutdown logic ---
// Note the new function signature.
void rr_create_processes(MemoryManager& mm) {
    process_maker_running = true;

    // This loop generates processes as long as the flag is true.
    while (process_maker_running) {
        std::shared_ptr<Process> pcb;
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            
            pcb = std::make_shared<Process>(cpuClocks++);
            pcb->start_time = std::chrono::system_clock::now();
            std::stringstream tempString;
            tempString << "process" << pcb->id;
            pcb->processName = tempString.str();

            // Give it a default memory size.
            size_t mem_size = 256;
            // Allocate its memory space in the backing store.
            mm.allocate_for_process(*pcb, mem_size);

            // Add some simple commands.
            pcb->commands.push_back("write 0x10 123");
            pcb->commands.push_back("read 0x10");
            
            rr_g_ready_queue.push_back(pcb);
        }
        
        rr_g_scheduler_cv.notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(processFrequency));
    }

    // --- CORRECTED SHUTDOWN LOGIC ---
    // After the process maker stops, this part waits for all existing processes to finish.
    while (true) {
        bool all_processes_are_finished;
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            // Check if any process is still in the ready or blocked queues.
            bool queues_are_empty = rr_g_ready_queue.empty() && rr_g_blocked_queue.empty();
            
            // Check if any process is still running on a core.
            bool running_is_empty = std::all_of(rr_g_running_processes.begin(), rr_g_running_processes.end(), 
                                                [](const auto& p){ return p == nullptr; });
            
            all_processes_are_finished = queues_are_empty && running_is_empty;
        }
        
        if (all_processes_are_finished) {
            // Once all work is done, break the loop.
            break;
        }
        // Wait for a second before checking again to avoid busy-waiting.
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // --- FINAL SHUTDOWN SIGNAL ---
    // This is the signal that tells the scheduler and worker threads to exit their loops.
    rr_g_is_running = false;
    rr_g_scheduler_cv.notify_all(); // Wake up all waiting threads so they can terminate.
}

// Function that starts and runs the processes
int RR() {
    // Create scheduler and worker threads
      rr_g_is_running = true; 
    std::thread scheduler(rr_scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < CPU_COUNT; ++i) {
        core_workers.emplace_back(rr_core_worker_func, i);
    }
    rr_g_scheduler_cv.notify_all();

    // Start process creation
    // rr_create_processes();

    // Shutdown
    scheduler.join();
    for (auto& worker : core_workers) {
        worker.join();
    }
    
    return 0;
}