#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <map>

// Enum for the process state
enum class ProcessState {
    READY,
    RUNNING,
    FINISHED
};

// A unified Process Control Block structure
struct PCB {
    int id;
    ProcessState state;
    std::vector<std::string> commands;
    size_t program_counter = 0;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point finish_time;
    int commands_executed_this_quantum;
    int assigned_core = -1;
    std::string processName = "";
    std::ofstream log_file;
    std::vector<std::string> rr_log_file;
    
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

#endif // PROCESS_H