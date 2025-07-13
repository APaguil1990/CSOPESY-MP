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

#include "ScreenManager.h"
#include "config.h"

// --- Configuration ---
const int NUM_PROCESSES = 10;
const int COMMANDS_PER_PROCESS = 100;

// --- Process State ---
enum class ProcessState {
    READY,
    RUNNING,
    FINISHED
};

// --- Process Control Block (PCB) ---
struct FCFS_PCB {
    int id;
    std::string processName = "";
    ProcessState state;
    std::vector<std::string> commands;
    size_t program_counter = 0;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point finish_time;
    int assigned_core = -1;
    std::vector<std::string> log_file;
};

std::random_device rd;  // a seed source for the random number engine
std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()

// --- Shared Data Structures ---
std::deque<std::shared_ptr<FCFS_PCB>> fcfs_g_ready_queue;
std::vector<std::shared_ptr<FCFS_PCB>> fcfs_g_running_processes(CPU_COUNT, nullptr);
std::vector<std::shared_ptr<FCFS_PCB>> fcfs_g_finished_processes;

// --- Synchronization Primitives ---
std::mutex fcfs_g_process_mutex;
std::condition_variable fcfs_g_scheduler_cv;
std::atomic<bool> fcfs_g_is_running(true);

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
void fcfs_scheduler_thread_func() {
    while (fcfs_g_is_running) {
        std::unique_lock<std::mutex> lock(fcfs_g_process_mutex);
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

        for (int i = 0; i < CPU_COUNT; ++i) {
            if (fcfs_g_running_processes[i] == nullptr && !fcfs_g_ready_queue.empty()) {
                std::shared_ptr<FCFS_PCB> process = fcfs_g_ready_queue.front();
                fcfs_g_ready_queue.pop_front();
                process->state = ProcessState::RUNNING;
                process->assigned_core = i;
                fcfs_g_running_processes[i] = process;
            }
        }
    }
}

// --- CPU Worker Thread Function ---
void fcfs_core_worker_func(int core_id) {
    while (fcfs_g_is_running) {
        std::shared_ptr<FCFS_PCB> my_process = nullptr;
        {
            std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
            my_process = fcfs_g_running_processes[core_id];
        }

        if (my_process) {
            // This is the "print command" execution
            if (my_process->program_counter < my_process->commands.size()) {
                const std::string& command = my_process->commands[my_process->program_counter];
                auto now = std::chrono::system_clock::now();



                if (command.compare("declare") == 0) {
                    fcfs_declareCommand();
                    
                } else if (command.compare("add") == 0) {
                    fcfs_addCommand();

                } else if (command.compare("sub") == 0) {
                    fcfs_subtractCommand();

                } else if (command.compare("sleep") == 0) {
                    fcfs_sleepCommand();

                } else if (command.compare("for") == 0) {
                    fcfs_forCommand();


                } else {
                    std::ostringstream tempString;
                    tempString << "(" << fcfs_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                               << " \"" << command << "\"" << std::endl;
                    my_process->log_file.push_back(tempString.str());
                }



                my_process->program_counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (my_process->program_counter >= my_process->commands.size()) {
                std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
                my_process->state = ProcessState::FINISHED;
                my_process->finish_time = std::chrono::system_clock::now();
                fcfs_g_finished_processes.push_back(my_process);
                fcfs_g_running_processes[core_id] = nullptr;
                fcfs_g_scheduler_cv.notify_one();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
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


void fcfs_create_process(std::string processName) {
    // Create a new process
    std::shared_ptr<FCFS_PCB> pcb;
    {
        std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
        
        pcb = std::make_shared<FCFS_PCB>(cpuClocks);
        pcb->start_time = std::chrono::system_clock::now();
        pcb->processName = processName;

        std::uniform_int_distribution<> instructionCount_rand(MIN_INS, MAX_INS);
        std::uniform_int_distribution<> instruction_rand(0, 5);

        int instructionCount = instructionCount_rand(gen);

        for (int j = 0; j < instructionCount; ++j) {
            std::stringstream fcfs_command_stream;
            int instruction = instruction_rand(gen);

            switch (instruction) {
                case 0: // print
                    fcfs_command_stream << "Hello world from process " << pcb->processName << "!";
                    pcb->commands.push_back(fcfs_command_stream.str());
                    break;
                case 1: // declare
                    fcfs_command_stream << "declare";
                    pcb->commands.push_back(fcfs_command_stream.str());
                    break;
                case 2: // add
                    fcfs_command_stream << "add";
                    pcb->commands.push_back(fcfs_command_stream.str());
                    break;
                case 3: // sub
                    fcfs_command_stream << "sub";
                    pcb->commands.push_back(fcfs_command_stream.str());
                    break;
                case 4: // sleep
                    fcfs_command_stream << "sleep";
                    pcb->commands.push_back(fcfs_command_stream.str());
                    break;
                case 5: // for
                    fcfs_command_stream << "for";
                    pcb->commands.push_back(fcfs_command_stream.str());
                    break;
            }
            cpuClocks++;
        }
        
        fcfs_g_ready_queue.push_back(pcb);
    }
    
    // Notify scheduler that a new process is available
    fcfs_g_scheduler_cv.notify_one();
}


// Function that creates processes
void fcfs_create_processes() {
    process_maker_running = true;
    auto rr_manager = ScreenManager::getInstance();

    // Process Generation Loop
    while (fcfs_g_is_running) {
        if (process_maker_running) {
            // Create a new process
            std::shared_ptr<FCFS_PCB> pcb;
            {
                std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
                
                pcb = std::make_shared<FCFS_PCB>(cpuClocks);
                pcb->start_time = std::chrono::system_clock::now();

                std::stringstream tempString;
                tempString << "process" << pcb->id;

                pcb->processName = tempString.str();

                std::uniform_int_distribution<> instructionCount_rand(MIN_INS, MAX_INS);
                std::uniform_int_distribution<> instruction_rand(0, 5);

                int instructionCount = instructionCount_rand(gen);

                for (int j = 0; j < instructionCount; ++j) {
                    std::stringstream rr_command_stream;
                    int instruction = instruction_rand(gen);

                    switch (instruction) {
                        case 0: // print
                            rr_command_stream << "Hello world from process p" << cpuClocks << "!";
                            pcb->commands.push_back(rr_command_stream.str());
                            break;
                        case 1: // declare
                            rr_command_stream << "declare";
                            pcb->commands.push_back(rr_command_stream.str());
                            break;
                        case 2: // add
                            rr_command_stream << "add";
                            pcb->commands.push_back(rr_command_stream.str());
                            break;
                        case 3: // sub
                            rr_command_stream << "sub";
                            pcb->commands.push_back(rr_command_stream.str());
                            break;
                        case 4: // sleep
                            rr_command_stream << "sleep";
                            pcb->commands.push_back(rr_command_stream.str());
                            break;
                        case 5: // for
                            rr_command_stream << "for";
                            pcb->commands.push_back(rr_command_stream.str());
                            break;
                    }
                    cpuClocks++;
                }
                
                fcfs_g_ready_queue.push_back(pcb);
            }
            
            // Notify scheduler that a new process is available
            fcfs_g_scheduler_cv.notify_one();
        }

        // Add some delay between process creation if needed
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Check if we should stop
        if (fcfs_g_ready_queue.empty() && fcfs_g_running_processes.empty()) {
            fcfs_g_is_running = false;
            fcfs_g_scheduler_cv.notify_all();
        }
    }
}

// Function that starts and runs the processes
int FCFS() {
    // Create scheduler and worker threads
    std::thread scheduler(fcfs_scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < CPU_COUNT; ++i) {
        core_workers.emplace_back(fcfs_core_worker_func, i);
    }
    fcfs_g_scheduler_cv.notify_all();

    // Start process creation
    fcfs_create_processes();

    // Shutdown
    scheduler.join();
    for (auto& worker : core_workers) {
        worker.join();
    }
    
    return 0;
}