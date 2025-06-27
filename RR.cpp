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
struct PCB {
    int id;
    int commands_executed_this_quantum;
    ProcessState state;
    std::vector<std::string> commands;
    size_t program_counter = 0;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point finish_time;
    int assigned_core = -1;
    std::ofstream log_file;

    PCB(int pid) : id(pid), state(ProcessState::READY) {
        std::stringstream ss;
        ss << "process" << (id < 10 ? "0" : "") << id << ".txt";
        log_file.open(ss.str());
    }

    ~PCB() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }
};

// --- Shared Data Structures ---
std::deque<std::shared_ptr<PCB>> g_ready_queue;
std::vector<std::shared_ptr<PCB>> g_running_processes(NUM_CORES, nullptr);
std::vector<std::shared_ptr<PCB>> g_finished_processes;

// --- Synchronization Primitives ---
std::mutex g_process_mutex;
std::condition_variable g_scheduler_cv;
std::atomic<bool> g_is_running(true);

// --- Helper Function to Format Time ---
std::string format_time(const std::chrono::system_clock::time_point& tp, const std::string& fmt) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, fmt.c_str());
    return ss.str();
}

// --- Scheduler Thread Function ---
void scheduler_thread_func() {
    while (g_is_running) {
        std::unique_lock<std::mutex> lock(g_process_mutex);
        g_scheduler_cv.wait(lock, [&]() {
            if (!g_is_running) return true;
            bool core_is_free = false;
            for (const auto& p : g_running_processes) {
                if (p == nullptr) {
                    core_is_free = true;
                    break;
                }
            }
            return core_is_free && !g_ready_queue.empty();
        });

        if (!g_is_running) break;

        for (int i = 0; i < NUM_CORES; ++i) {
            if (g_running_processes[i] == nullptr && !g_ready_queue.empty()) {
                std::shared_ptr<PCB> process = g_ready_queue.front();
                g_ready_queue.pop_front();
                process->state = ProcessState::RUNNING;
                process->assigned_core = i;
                process->commands_executed_this_quantum = 0;
                g_running_processes[i] = process;
            }
        }
    }
}

// --- CPU Worker Thread Function ---
void core_worker_func(int core_id) { // executes cmds
    while (g_is_running) {
        std::shared_ptr<PCB> my_process = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_process_mutex);
            my_process = g_running_processes[core_id];
        }

        if (my_process) {
            // This is the "print command" execution   REPLACE WITH THE RANDOM COMMANDS THING
            if (my_process->program_counter < my_process->commands.size()) {
                const std::string& command = my_process->commands[my_process->program_counter];
                auto now = std::chrono::system_clock::now();



                my_process->log_file << "(" << format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                                     << " \"" << command << "\"" << std::endl;



                my_process->program_counter++;
                my_process->commands_executed_this_quantum++;

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            std::unique_lock<std::mutex> lock(g_process_mutex);
            if (my_process->commands_executed_this_quantum >= TIME_QUANTUM && my_process->program_counter < my_process->commands.size()) {
                // Preempt the process and put it back in the ready queue
                my_process->state = ProcessState::READY;
                g_ready_queue.push_back(my_process);
                g_running_processes[core_id] = nullptr;
                g_scheduler_cv.notify_one();
            } 
            else if (my_process->program_counter >= my_process->commands.size()) {
                // Process has completed all commands
                my_process->state = ProcessState::FINISHED;
                my_process->finish_time = std::chrono::system_clock::now();
                g_finished_processes.push_back(my_process);
                g_running_processes[core_id] = nullptr;
                g_scheduler_cv.notify_one();
            }
            lock.unlock();

        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// --- UI Function to Display Process Lists ---
void display_processes() {
    std::lock_guard<std::mutex> lock(g_process_mutex);
    std::cout << "\n-------------------------------------------------------------\n";


    std::cout << "Process Queue:\n";
    for (const auto& p : g_ready_queue) {
        if (p) {
            std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                      << " (" << format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                      << "\tCore: " << p->assigned_core
                      << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }

    std::cout << "\nRunning processes:\n";
    for (const auto& p : g_running_processes) {
        if (p) {
            std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                      << " (" << format_time(p->start_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                      << "\tCore: " << p->assigned_core
                      << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
        }
    }

    std::cout << "\nFinished processes:\n";
    for (const auto& p : g_finished_processes) {
        std::cout << "process" << (p->id < 10 ? "0" : "") << std::to_string(p->id)
                  << " (" << format_time(p->finish_time, "%m/%d/%Y %I:%M:%S%p") << ")"
                  << "\tFinished"
                  << "\t" << p->program_counter << " / " << p->commands.size() << std::endl;
    }
    std::cout << "-------------------------------------------------------------\n\n";
}

void declareCommand() {
    switch (std::rand()%3) {
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
    cpuClocks += std::rand()%65536;
}

void forCommand() {
    std::cout << "idk how to do this";
}

int RR() {

    bool schedulerRunning = false;
    int replaceLoopNum = 1;

    // TODO: process generation should go here i think use a while statement with the config
    while (schedulerRunning) {
        std::lock_guard<std::mutex> lock(g_process_mutex);
        auto pcb = std::make_shared<PCB>(replaceLoopNum); // something has to increment here but we dont have a for loop anymore
        pcb->start_time = std::chrono::system_clock::now();
        
        //generate process
        std::stringstream command_stream;
        // do thing here random
        int instruction = std::rand()%6;

        switch (instruction) {
            case 0:
                std::cout << "PRINT COMMAND";
                break;
            case 1:
                std::cout << "DECLARE COMMAND";
                declareCommand();
                break;
            case 2:
                std::cout << "ADD COMMAND";
                addCommand();
                break;
            case 3:
                std::cout << "SUBTRACT COMMAND";
                subtractCommand();
                break;
            case 4:
                std::cout << "SLEEP COMMAND";
                sleepCommand();
                break;
            case 5:
                std::cout << "FOR COMMAND";
                forCommand();
                break;
        }

        command_stream << "cmd log here";

        // etc etc this part adds the instructions stated into ccommand stream
        // line after this one pushes the stream into commands in pcb. after that, push the pcb into the ready queue
    }




    std::cout << "OS Emulator with RR Scheduler starting..." << std::endl;
    std::cout << "Type 'screen -ls' to see process status." << std::endl;
    std::cout << "Type 'exit' to terminate." << std::endl;

    // --- Create Initial Processes ---
    {
        std::lock_guard<std::mutex> lock(g_process_mutex);
        for (int i = 1; i <= NUM_PROCESSES; ++i) {
            auto pcb = std::make_shared<PCB>(i);
            pcb->start_time = std::chrono::system_clock::now();

            if (i == 10) {
                for (int j = 0; j < 50; ++j) {
                    std::stringstream command_stream;
                    command_stream << "Hello from process " << i << ", line " << j + 1;
                    pcb->commands.push_back(command_stream.str());
                }
                g_ready_queue.push_back(pcb);
            }
            if (i == 7 || i == 8 || i == 9) {
                for (int j = 0; j < 75; ++j) {
                    std::stringstream command_stream;
                    command_stream << "Hello from process " << i << ", line " << j + 1;
                    pcb->commands.push_back(command_stream.str());
                }
                g_ready_queue.push_back(pcb);
            }
            if (i == 4 || i == 5 || i == 6) {
                for (int j = 0; j < 100; ++j) {
                    std::stringstream command_stream;
                    command_stream << "Hello from process " << i << ", line " << j + 1;
                    pcb->commands.push_back(command_stream.str());
                }
                g_ready_queue.push_back(pcb);
            }
            if (i == 1 || i == 2 || i == 3) {
                for (int j = 0; j < 125; ++j) {
                    std::stringstream command_stream;
                    command_stream << "Hello from process " << i << ", line " << j + 1;
                    pcb->commands.push_back(command_stream.str());
                }
                g_ready_queue.push_back(pcb);
            }

            
        }
    }

    // --- Launch Threads ---
    std::thread scheduler(scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < NUM_CORES; ++i) {
        core_workers.emplace_back(core_worker_func, i);
    }
    g_scheduler_cv.notify_all();

    // --- Main UI Loop  ---
    std::string command;
    while (g_is_running) {
        std::cout << "> ";
        std::getline(std::cin, command);

        if (command == "screen -ls") {
            display_processes();
        } else if (command == "exit") {
            g_is_running = false;
            g_scheduler_cv.notify_all(); // Wake up all waiting threads
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