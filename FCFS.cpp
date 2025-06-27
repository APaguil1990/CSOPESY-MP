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

// --- Configuration ---
const int NUM_CORES = 4;
const int NUM_PROCESSES = 10;
const int COMMANDS_PER_PROCESS = 100;

// --- Process State ---
enum class ProcessState {
    READY,
    RUNNING,
    FINISHED
};

// --- Process Control Block (PCB) ---
struct PCB {
    int id;
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
std::deque<std::shared_ptr<PCB>> fcfs_g_ready_queue;
std::vector<std::shared_ptr<PCB>> fcfs_g_running_processes(NUM_CORES, nullptr);
std::vector<std::shared_ptr<PCB>> fcfs_g_finished_processes;

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

        for (int i = 0; i < NUM_CORES; ++i) {
            if (fcfs_g_running_processes[i] == nullptr && !fcfs_g_ready_queue.empty()) {
                std::shared_ptr<PCB> process = fcfs_g_ready_queue.front();
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
        std::shared_ptr<PCB> my_process = nullptr;
        {
            std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
            my_process = fcfs_g_running_processes[core_id];
        }

        if (my_process) {
            // This is the "print command" execution
            if (my_process->program_counter < my_process->commands.size()) {
                const std::string& command = my_process->commands[my_process->program_counter];
                auto now = std::chrono::system_clock::now();
                my_process->log_file << "(" << fcfs_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                                     << " \"" << command << "\"" << std::endl;
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

int FCFS() {
    // std::cout << "OS Emulator with FCFS Scheduler starting..." << std::endl;
    // std::cout << "Type 'screen -ls' to see process status." << std::endl;
    // std::cout << "Type 'exit' to terminate." << std::endl;

    // --- Create Initial Processes ---
    {
        std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
        for (int i = 1; i <= NUM_PROCESSES; ++i) {
            auto pcb = std::make_shared<PCB>(i);
            pcb->start_time = std::chrono::system_clock::now();
            for (int j = 0; j < COMMANDS_PER_PROCESS; ++j) {
                std::stringstream command_stream;
                command_stream << "Hello from process " << i << ", line " << j + 1;
                pcb->commands.push_back(command_stream.str());
            }
            fcfs_g_ready_queue.push_back(pcb);
        }
    }

    // --- Launch Threads ---
    std::thread scheduler(fcfs_scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < NUM_CORES; ++i) {
        core_workers.emplace_back(fcfs_core_worker_func, i);
    }
    fcfs_g_scheduler_cv.notify_all();

    // --- Main UI Loop  ---
    std::string command;
    while (fcfs_g_is_running) {
        if (command == "exit") {
            fcfs_g_is_running = false;
            fcfs_g_scheduler_cv.notify_all(); // Wake up all waiting threads
        }
    }

    // --- Shutdown ---
    std::cout << "Shutting down. Waiting for threads to complete..." << std::endl;
    scheduler.join();
    for (auto& worker : core_workers) {
        worker.join();
    }
    std::cout << "Scheduler terminated." << std::endl;

    return 0;
}