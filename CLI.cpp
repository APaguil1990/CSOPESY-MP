#include <iostream>
#include <fstream>
#include <string>
#include <windows.h>
#include <vector>
#include <algorithm>
#include <thread>
#include <sstream>
#include <atomic>
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <iomanip>

#include "global.h"
#include "FCFS.h"
#include "RR.h"
#include "Process.h"
#include "MemoryManager.h"
#include "ProcessSMI.h"
#include "vmstat.h"

using namespace std;

// --- Global Variable Definitions ---
MemoryManager* memory_manager = nullptr;
std::deque<ProcessCreationRequest> g_creation_queue;
std::mutex g_creation_queue_mutex;
std::unordered_map<std::string, std::shared_ptr<Process>> g_process_map;
std::mutex g_process_map_mutex;
std::atomic<bool> g_system_initialized = false;
std::atomic<bool> g_is_shutting_down = false;
std::mutex g_cout_mutex;

std::deque<std::shared_ptr<Process>> rr_g_ready_queue;
std::vector<std::shared_ptr<Process>> rr_g_running_processes;
std::vector<std::shared_ptr<Process>> rr_g_finished_processes;
std::deque<std::shared_ptr<Process>> rr_g_blocked_queue;
std::mutex rr_g_process_mutex;
std::condition_variable rr_g_scheduler_cv;
std::atomic<bool> rr_g_is_running(true);

std::deque<std::shared_ptr<Process>> fcfs_g_ready_queue;
std::vector<std::shared_ptr<Process>> fcfs_g_running_processes;
std::vector<std::shared_ptr<Process>> fcfs_g_finished_processes;
std::deque<std::shared_ptr<Process>> fcfs_g_blocked_queue;
std::mutex fcfs_g_process_mutex;
std::condition_variable fcfs_g_scheduler_cv;
std::atomic<bool> fcfs_g_is_running(true);

std::atomic<bool> process_maker_running = false;
std::atomic<int> cpuClocks = 1;

// Config parameters
int CPU_COUNT = 0;
string scheduler = "";
int qCycles = 0;
int processFrequency = 0;
int MIN_INS = 0;
int MAX_INS = 0;
int delayPerExec = 0;
int MAX_OVERALL_MEM = 0;
int MEM_PER_FRAME = 0;
int MIN_MEM_PER_PROC = 0;
int MAX_MEM_PER_PROC = 0;

// Forward declarations
void printHeader();
void clearScreen();
bool readConfig();
string processCommand(const string& cmd);
bool isValidMemorySize(size_t size);

// --- Main Program Entry ---
int main() {
    clearScreen();
    printHeader();
    cout << "Welcome to CSOPESY! Type 'initialize' to begin." << endl;

    std::thread scheduler_thread;
    std::thread generator_thread;
    bool system_threads_launched = false;

    string input;
    while (!g_is_shutting_down) {
        cout << "\nroot:\\> ";
        if (!getline(cin, input)) {
            g_is_shutting_down = true;
            break;
        }

        if (input == "exit") {
             g_is_shutting_down = true;
             break;
        }

        if (!g_system_initialized && input != "initialize") {
            cout << "System not initialized. Please run 'initialize' first." << endl;
            continue;
        }

        string cmd_output = processCommand(input);

        if (!cmd_output.empty()) {
            cout << cmd_output << endl;
        }

        // Launch scheduler and generator after successful initialization
        if (g_system_initialized && !system_threads_launched) {
            if (scheduler == "rr") {
                scheduler_thread = std::thread(RR);
                generator_thread = std::thread(rr_create_processes, std::ref(*memory_manager));
            } else if (scheduler == "fcfs") {
                // FCFS can be added here
                cout << "FCFS scheduler is not fully integrated for auto-generation in this version." << endl;
            }
            system_threads_launched = true;
        }
    }

    // --- Shutdown Sequence ---
    cout << "\nShutting down..." << endl;
    g_is_shutting_down = true;
    process_maker_running = false;
    rr_g_is_running = false;
    fcfs_g_is_running = false;

    rr_g_scheduler_cv.notify_all();
    fcfs_g_scheduler_cv.notify_all();

    if (generator_thread.joinable()) generator_thread.join();
    if (scheduler_thread.joinable()) scheduler_thread.join();

    delete memory_manager;
    cout << "Program finished." << endl;
    return 0;
}

// --- Command Processing Logic ---
string processCommand(const string& cmd) {
    vector<string> tokens;
    istringstream iss(cmd);
    string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    if (tokens.empty()) return "";

    // --- INITIALIZE ---
    if (tokens[0] == "initialize") {
        if (g_system_initialized) return "System is already initialized.";
        if (!readConfig()) return "Initialization failed: Could not read or parse config.txt.";

        memory_manager = new MemoryManager(rr_g_ready_queue, rr_g_running_processes, fcfs_g_ready_queue, fcfs_g_running_processes);
        g_system_initialized = true;
        return "System initialized successfully. Scheduler: '" + scheduler + "'.";
    }

    // --- SCHEDULER-START/STOP ---
    if (tokens[0] == "scheduler-start") {
        if (process_maker_running) return "Generator is already running.";
        process_maker_running = true;
        return "Process generator started.";
    }
    if (tokens[0] == "scheduler-stop") {
        if (!process_maker_running) return "Generator is not running.";
        process_maker_running = false;
        return "Process generator stopped.";
    }

    // --- SCREEN ---
    if (tokens[0] == "screen") {
        if (tokens.size() < 2) return "Invalid screen command. Use 'screen -s', 'screen -c', 'screen -r', or 'screen -ls'.";

        // screen -s <name> <mem_size>
        if (tokens[1] == "-s" && tokens.size() == 4) {
            try {
                size_t mem_size = stoull(tokens[3]);
                if (!isValidMemorySize(mem_size)) return "Invalid memory allocation: Must be a power of 2 between 64 and 65536 bytes.";

                {
                    std::lock_guard<std::mutex> lock(g_process_map_mutex);
                    if (g_process_map.count(tokens[2])) return "Error: Process name '" + tokens[2] + "' already exists.";
                }
                {
                    std::lock_guard<std::mutex> lock(g_creation_queue_mutex);
                    g_creation_queue.push_back({tokens[2], mem_size, {}}); // No custom commands
                }
                rr_g_scheduler_cv.notify_one();
                return "Process creation request for '" + tokens[2] + "' submitted.";
            } catch (...) {
                return "Invalid memory size format. Please use an integer.";
            }
        }
        // screen -c <name> <mem_size> "<instructions>"
        if (tokens[1] == "-c" && tokens.size() >= 5) {
            try {
                size_t mem_size = stoull(tokens[3]);
                if (!isValidMemorySize(mem_size)) return "Invalid memory allocation: Must be a power of 2 between 64 and 65536 bytes.";

                size_t instruction_start_pos = cmd.find(tokens[4]);
                std::string instruction_string = cmd.substr(instruction_start_pos);

                if (instruction_string.front() != '"' || instruction_string.back() != '"') {
                    return "Invalid 'screen -c' format: Instructions must be enclosed in double quotes.";
                }
                instruction_string = instruction_string.substr(1, instruction_string.length() - 2);

                std::vector<std::string> individual_commands;
                std::stringstream ss(instruction_string);
                std::string segment;
                while (std::getline(ss, segment, ';')) {
                    segment.erase(0, segment.find_first_not_of(" \t\n\r"));
                    segment.erase(segment.find_last_not_of(" \t\n\r") + 1);
                    if (!segment.empty()) individual_commands.push_back(segment);
                }

                if (individual_commands.empty() || individual_commands.size() > 50) {
                    return "Invalid command: Instruction count must be between 1 and 50.";
                }
                {
                    std::lock_guard<std::mutex> lock(g_process_map_mutex);
                    if (g_process_map.count(tokens[2])) return "Error: Process name '" + tokens[2] + "' already exists.";
                }
                {
                    std::lock_guard<std::mutex> lock(g_creation_queue_mutex);
                    g_creation_queue.push_back({tokens[2], mem_size, individual_commands});
                }
                rr_g_scheduler_cv.notify_one();
                return "Request for process '" + tokens[2] + "' with custom instructions submitted.";
            } catch (...) {
                return "Invalid 'screen -c' format. Usage: screen -c <name> <size> \"<instr>\"";
            }
        }
        // screen -r <name>
        if (tokens[1] == "-r" && tokens.size() == 3) {
            std::shared_ptr<Process> p;
            {
                std::lock_guard<std::mutex> lock(g_process_map_mutex);
                if (g_process_map.count(tokens[2])) {
                    p = g_process_map.at(tokens[2]);
                }
            }

            if (!p) return "Process '" + tokens[2] + "' not found.";

            if (p->state == ProcessState::TERMINATED && p->mem_data.terminated_by_error) {
                auto t = std::chrono::system_clock::to_time_t(p->mem_data.termination_time);
                std::tm tm_buf;
                localtime_s(&tm_buf, &t);
                std::stringstream time_ss, addr_ss;
                time_ss << std::put_time(&tm_buf, "%H:%M:%S");
                addr_ss << "0x" << std::hex << p->mem_data.invalid_address;

                return "Process " + p->processName + " shut down due to memory access violation error that occurred at "
                       + time_ss.str() + ". " + addr_ss.str() + " invalid.";
            }

            std::stringstream result_ss;
            result_ss << "\n--- Process: " << p->processName << " (ID: " << p->id << ") ---\n";
            result_ss << "Status: ";
            switch(p->state){
                case ProcessState::RUNNING: result_ss << "Running on Core " << p->assigned_core; break;
                case ProcessState::READY: result_ss << "Ready"; break;
                case ProcessState::BLOCKED: result_ss << "Blocked (Awaiting Page)"; break;
                case ProcessState::FINISHED: result_ss << "Finished!"; break;
                case ProcessState::TERMINATED: result_ss << "Terminated (Error)"; break;
                default: result_ss << "New"; break;
            }
            result_ss << "\nInstruction: " << p->program_counter << " / " << p->commands.size();
            result_ss << "\nLogs from PRINT instructions:\n";
            if (p->output_logs.empty()) {
                result_ss << "  (No output logs generated yet)\n";
            } else {
                for(const auto& log : p->output_logs) {
                    result_ss << "  " << log << "\n";
                }
            }
            return result_ss.str();
        }
        // screen -ls
        if (tokens[1] == "-ls" && tokens.size() == 2) {
            std::stringstream result_ss;
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            
            int busy_cores = 0;
            for(const auto& proc : rr_g_running_processes) {
                if (proc) busy_cores++;
            }
            int cpu_util = (CPU_COUNT > 0) ? (100 * busy_cores / CPU_COUNT) : 0;
            
            result_ss << "\n--- System Status ---\n";
            result_ss << "CPU Utilization: " << cpu_util << "%\n";
            result_ss << "Cores Used: " << busy_cores << " / Cores Available: " << (CPU_COUNT - busy_cores) << "\n";

            result_ss << "\nRunning Processes (" << busy_cores << "):\n";
            for (const auto& p : rr_g_running_processes) {
                if (p) result_ss << "  " << p->processName << " (Core: " << p->assigned_core << ")\n";
            }
            result_ss << "\nReady Queue (" << rr_g_ready_queue.size() << " processes)\n";
            result_ss << "Blocked Queue (" << rr_g_blocked_queue.size() << " processes)\n";
            
            result_ss << "\nFinished/Terminated Processes (" << rr_g_finished_processes.size() << "):\n";
            for (const auto& p : rr_g_finished_processes) {
                if(p) result_ss << "  " << p->processName << (p->state == ProcessState::TERMINATED ? " (Terminated)" : " (Finished)") << "\n";
            }
            return result_ss.str();
        }
        return "Invalid 'screen' command syntax.";
    }

    // --- VMSTAT & PROCESS-SMI ---
    if (tokens[0] == "vmstat") {
        if (!memory_manager) return "System not initialized.";
        std::stringstream ss;
        ss << "\n--- VMSTAT ---\n"
           << "Total memory      : " << MAX_OVERALL_MEM << " bytes\n"
           << "Used memory       : " << memory_manager->get_used_memory_bytes() << " bytes\n"
           << "Free memory       : " << memory_manager->get_free_memory_bytes() << " bytes\n"
           << "Idle CPU ticks    : " << get_idle_cpu_ticks() << "\n"
           << "Active CPU ticks  : " << get_active_cpu_ticks() << "\n"
           << "Total CPU ticks   : " << get_total_cpu_ticks() << "\n"
           << "Num paged in      : " << memory_manager->pages_paged_in.load() << "\n"
           << "Num paged out     : " << memory_manager->pages_paged_out.load() << "\n";
        return ss.str();
    }
    if (tokens[0] == "process-smi") {
        process_smi::printSnapshot();
        return ""; // The function prints directly to the console
    }

    // --- REPORT-UTIL ---
    if (tokens[0] == "report-util") {
        std::ofstream log_file("csopesy-log.txt", std::ios::app);
        if(!log_file.is_open()) return "Error: Could not open csopesy-log.txt for writing.";
        
        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        
        log_file << "\n--- REPORT GENERATED AT " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << " ---\n";
        log_file << processCommand("screen -ls"); // Reuse the logic from screen -ls
        log_file.close();

        return "Report written to csopesy-log.txt";
    }

    // --- CLEAR ---
    if (tokens[0] == "clear") {
        clearScreen();
        printHeader();
        return "";
    }

    return "Unknown command: '" + cmd + "'";
}

// --- Helper Functions ---
bool isValidMemorySize(size_t size) {
    if (size < 64 || size > 65536) return false;
    return (size > 0) && ((size & (size - 1)) == 0); // Check if it's a power of 2
}

bool readConfig() {
    ifstream configFile("config.txt");
    if (!configFile.is_open()) return false;

    string key, value;
    while (configFile >> key) {
        // For scheduler, the value might have quotes
        if (key == "scheduler") {
            configFile >> std::ws; // consume whitespace
            std::getline(configFile, value);
            if (value.front() == '"') value = value.substr(1);
            if (value.back() == '"') value.pop_back();
        } else {
            configFile >> value;
        }

        try {
            if (key == "num-cpu") CPU_COUNT = std::stoi(value);
            else if (key == "scheduler") scheduler = value;
            else if (key == "quantum-cycles") qCycles = std::stoi(value);
            else if (key == "batch-process-freq") processFrequency = std::stoi(value);
            else if (key == "min-ins") MIN_INS = std::stoi(value);
            else if (key == "max-ins") MAX_INS = std::stoi(value);
            else if (key == "delay-per-exec") delayPerExec = std::stoi(value);
            else if (key == "max-overall-mem") MAX_OVERALL_MEM = std::stoi(value);
            else if (key == "mem-per-frame") MEM_PER_FRAME = std::stoi(value);
            else if (key == "min-mem-per-proc") MIN_MEM_PER_PROC = std::stoi(value);
            else if (key == "max-mem-per-proc") MAX_MEM_PER_PROC = std::stoi(value);
        } catch(...) {
            configFile.close();
            return false; // Error parsing a value
        }
    }
    configFile.close();
    rr_g_running_processes.resize(CPU_COUNT, nullptr);
    fcfs_g_running_processes.resize(CPU_COUNT, nullptr);
    return true;
}

void clearScreen() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD topLeft = {0, 0};
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    DWORD written;
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written);
    SetConsoleCursorPosition(hConsole, topLeft);
}

void printHeader() {
    const vector<string> hardcodedHeader = {
        R"(________/\\\\\\\\\_____/\\\\\\\\\\\_________/\\\\\_______/\\\\\\\\\\\\\____/\\\\\\\\\\\\\\\_____/\\\\\\\\\\\____/\\\________/\\\_)",
        R"( _____/\\\////////____/\\\/////////\\\_____/\\\///\\\____\/\\\/////////\\\_\/\\\///////////____/\\\/////////\\\_\///\\\____/\\\/__)",
        R"(  ___/\\\/____________\//\\\______\///____/\\\/__\///\\\__\/\\\_______\/\\\_\/\\\______________\//\\\______\///____\///\\\/\\\/____)",
        R"(   __/\\\_______________\////\\\__________/\\\______\//\\\_\/\\\\\\\\\\\\\/__\/\\\\\\\\\\\_______\////\\\_____________\///\\\/______)",
        R"(    _\/\\\__________________\////\\\______\/\\\_______\/\\\_\/\\\/////////____\/\\\///////___________\////\\\____________\/\\\_______)",
        R"(     _\//\\\____________________\////\\\___\//\\\______/\\\__\/\\\_____________\/\\\_____________________\////\\\_________\/\\\_______)",
        R"(      __\///\\\___________/\\\______\//\\\___\///\\\__/\\\____\/\\\_____________\/\\\______________/\\\______\//\\\________\/\\\_______)",
        R"(       ____\////\\\\\\\\\_\///\\\\\\\\\\\/______\///\\\\\/_____\/\\\_____________\/\\\\\\\\\\\\\\\_\///\\\\\\\\\\\/_________\/\\\_______)",
        R"(        _______\/////////____\///////////__________\/////_______\///______________\///////////////____\///////////___________\///________)"
    };
    cout << "\n";
    for(const auto& line : hardcodedHeader) {
        cout << line << endl;
    }
    cout << "\n";
}