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

#include "config.h"

// --- Configuration ---
const int NUM_CORES = 4;
const int NUM_PROCESSES = 10;
const int COMMANDS_PER_PROCESS = 105;
const int TIME_QUANTUM = 3;

// --- Process State ---
enum class ProcessState {
    READY,
    RUNNING,
    FINISHED
};

// --- Process Control Block (PCB) ---
struct RR_PCB {
    int id;
    int commands_executed_this_quantum;
    ProcessState state;
    std::vector<std::string> commands;
    size_t program_counter = 0;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point finish_time;
    int assigned_core = -1;
    std::ofstream log_file;

    RR_PCB(int pid) : id(pid), state(ProcessState::READY) {
        std::stringstream ss;
        ss << "process" << (id < 10 ? "0" : "") << id << ".txt";
        log_file.open(ss.str());
    }

    ~RR_PCB() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }
};

std::random_device rd;  // a seed source for the random number engine
std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()

// --- Shared Data Structures ---
std::deque<std::shared_ptr<RR_PCB>> rr_g_ready_queue;
std::vector<std::shared_ptr<RR_PCB>> rr_g_running_processes(NUM_CORES, nullptr);
std::vector<std::shared_ptr<RR_PCB>> rr_g_finished_processes;

// --- Synchronization Primitives ---
std::mutex rr_g_process_mutex;
std::condition_variable rr_g_scheduler_cv;
std::atomic<bool> rr_g_is_running(true);

// --- Helper Function to Format Time ---
std::string rr_format_time(const std::chrono::system_clock::time_point& tp, const std::string& fmt) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, fmt.c_str());
    return ss.str();
}

void declareCommand() {
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

void addCommand() {
    variable_a = variable_b + variable_c;
}

void subtractCommand() {
    variable_a = variable_b - variable_c;
}

void sleepCommand() {
    cpuClocks += 10;
}

void forCommand() {
    for (int i; i < 5; i++) {
        addCommand();
    }
}

// --- Scheduler Thread Function ---
void rr_scheduler_thread_func() {
    while (rr_g_is_running) {
        std::unique_lock<std::mutex> lock(rr_g_process_mutex);
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

        for (int i = 0; i < NUM_CORES; ++i) {
            if (rr_g_running_processes[i] == nullptr && !rr_g_ready_queue.empty()) {
                std::shared_ptr<RR_PCB> process = rr_g_ready_queue.front();
                rr_g_ready_queue.pop_front();
                process->state = ProcessState::RUNNING;
                process->assigned_core = i;
                process->commands_executed_this_quantum = 0;
                rr_g_running_processes[i] = process;
            }
        }
    }
}

// --- CPU Worker Thread Function ---
void rr_core_worker_func(int core_id) { // executes cmds
    while (rr_g_is_running) {
        std::shared_ptr<RR_PCB> my_process = nullptr;
        {
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            my_process = rr_g_running_processes[core_id];
        }

        if (my_process) {
            // This is the "print command" execution   REPLACE WITH THE RANDOM COMMANDS THING
            if (my_process->program_counter < my_process->commands.size()) {
                const std::string& command = my_process->commands[my_process->program_counter];
                auto now = std::chrono::system_clock::now();


                if (command.compare("declare") == 0) {
                    declareCommand();
                    
                    // my_process->log_file << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                    //                      << " \"" << command << "\"" << std::endl;
                } else if (command.compare("add") == 0) {
                    addCommand();

                    // my_process->log_file << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                    //                      << " \"" << command << "\"" << std::endl;
                } else if (command.compare("sub") == 0) {
                    subtractCommand();

                    // my_process->log_file << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                    //                      << " \"" << command << "\"" << std::endl;
                } else if (command.compare("sleep") == 0) {
                    sleepCommand();

                    // my_process->log_file << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                    //                      << " \"" << command << "\"" << std::endl;
                } else if (command.compare("for") == 0) {
                    forCommand();

                    // my_process->log_file << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                    //                      << " \"" << command << "\"" << std::endl;
                } else {
                    my_process->log_file << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                                         << " \"" << command << "\"" << std::endl;
                }


                // my_process->log_file << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                //                      << " \"" << command << "\"" << std::endl;



                my_process->program_counter++;
                my_process->commands_executed_this_quantum++;

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            std::unique_lock<std::mutex> lock(rr_g_process_mutex);
            if (my_process->commands_executed_this_quantum >= TIME_QUANTUM && my_process->program_counter < my_process->commands.size()) {
                // Preempt the process and put it back in the ready queue
                my_process->state = ProcessState::READY;
                rr_g_ready_queue.push_back(my_process);
                rr_g_running_processes[core_id] = nullptr;
                rr_g_scheduler_cv.notify_one();
            } 
            else if (my_process->program_counter >= my_process->commands.size()) {
                // Process has completed all commands
                my_process->state = ProcessState::FINISHED;
                my_process->finish_time = std::chrono::system_clock::now();
                rr_g_finished_processes.push_back(my_process);
                rr_g_running_processes[core_id] = nullptr;
                rr_g_scheduler_cv.notify_one();
            }
            lock.unlock();

        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// --- UI Function to Display Process Lists ---
void rr_display_processes() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
    std::cout << "\n-------------------------------------------------------------\n";


    std::cout << "Process Queue:\n";
    for (const auto& p : rr_g_ready_queue) {
        if (p) {
            std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                      << " (" << rr_format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                      << "\tCore: " << p->assigned_core
                      << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }

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

int RR() {

    // --- Create Initial Processes ---
    {
        std::lock_guard<std::mutex> lock(rr_g_process_mutex);

        for (int i = 1; i <= NUM_PROCESSES; ++i) {
            auto pcb = std::make_shared<RR_PCB>(cpuClocks); // change cpuclock and i
            pcb->start_time = std::chrono::system_clock::now();

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
            rr_g_ready_queue.push_back(pcb);

        }
    }



    // --- Launch Threads ---
    std::thread scheduler(rr_scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < NUM_CORES; ++i) {
        core_workers.emplace_back(rr_core_worker_func, i);
    }
    rr_g_scheduler_cv.notify_all();

    // --- Main UI Loop  ---
    std::string command;
    // rr_display_processes();

    while (rr_g_is_running) {
        // std::cout << "> ";
        // std::getline(std::cin, command);
        
        if (command == "screen -ls") {
            rr_display_processes();
        } else if (command == "exit") {
            rr_g_is_running = false;
            rr_g_scheduler_cv.notify_all(); // Wake up all waiting threads
        } else if (!command.empty()) {
            std::cout << "Unknown command: '" << command << "'" << std::endl;
        }
    }

    // --- Shutdown ---
    std::cout << "Shutting down. Waiting for threads to complete..." << std::endl;
    scheduler.join();
    for (auto& worker : core_workers) {
        worker.join();
    }
    std::cout << "Emulator terminated." << std::endl;

    return 0;
}