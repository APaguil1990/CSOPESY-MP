#include <iostream>
#include <ostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <random>
#include <map> 
#include <algorithm>
#include <thread>
#include <cstdint>

#include "global.h" 
#include "RR.h"      
#include "vmstat.h"  

std::random_device rr_rd;
std::mt19937 rr_gen(rr_rd());

void rr_scheduler_thread_func();
void rr_core_worker_func(int core_id);
uint16_t parse_value_or_variable(const std::string& token, Process& process);

int RR() {
    rr_g_is_running = true;
    
    std::thread scheduler(rr_scheduler_thread_func);
    std::vector<std::thread> core_workers;
    for (int i = 0; i < CPU_COUNT; ++i) {
        core_workers.emplace_back(rr_core_worker_func, i);
    }
    
    scheduler.join();
    for (auto& worker : core_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    return 0;
}

void rr_scheduler_thread_func() {
    while (rr_g_is_running) {
        std::unique_lock<std::mutex> lock(rr_g_process_mutex);

        rr_g_scheduler_cv.wait(lock, [&]() {
            if (!rr_g_is_running) return true;
            bool core_is_free = std::any_of(rr_g_running_processes.begin(), rr_g_running_processes.end(), [](const auto& p){ return p == nullptr; });
            return !g_creation_queue.empty() || !rr_g_blocked_queue.empty() || (core_is_free && !rr_g_ready_queue.empty());
        });

        if (!rr_g_is_running) break;

        while (!g_creation_queue.empty()) {
            ProcessCreationRequest request = g_creation_queue.front();
            g_creation_queue.pop_front();
            
            lock.unlock(); 
            
            std::shared_ptr<Process> pcb = std::make_shared<Process>(cpuClocks++, request.name);
            memory_manager->allocate_for_process(*pcb, request.memory_size);

            if (request.commands.empty()) {
                std::uniform_int_distribution<> instr_dist(MIN_INS, MAX_INS);
                int num_instr = instr_dist(rr_gen);
                for (int i = 0; i < num_instr; ++i) {
                     pcb->commands.push_back("READ 0x0");
                }
            } else {
                pcb->commands = request.commands;
            }
            
            lock.lock();
            rr_g_ready_queue.push_back(pcb);
            {
                std::lock_guard<std::mutex> map_lock(g_process_map_mutex);
                g_process_map[pcb->processName] = pcb;
            }
        }

        auto it = rr_g_blocked_queue.begin();
        while (it != rr_g_blocked_queue.end()) {
            auto& process = *it;
            process->state = ProcessState::READY;
            rr_g_ready_queue.push_back(process);
            it = rr_g_blocked_queue.erase(it);
        }

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

void rr_core_worker_func(int core_id) {
    while (rr_g_is_running) {
        std::shared_ptr<Process> my_process;

        { 
            std::lock_guard<std::mutex> lock(rr_g_process_mutex);
            my_process = rr_g_running_processes[core_id];
        }

        if (!my_process) {
            vmstats_increment_idle_ticks();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (static_cast<size_t>(my_process->program_counter) >= my_process->commands.size() || my_process->state == ProcessState::TERMINATED) {
             {
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                my_process->state = (my_process->state == ProcessState::TERMINATED) ? ProcessState::TERMINATED : ProcessState::FINISHED;
                if(my_process->state == ProcessState::FINISHED) my_process->finish_time = std::chrono::system_clock::now();
                rr_g_finished_processes.push_back(my_process);
                rr_g_running_processes[core_id] = nullptr;
                memory_manager->deallocate_for_process(*my_process);
                rr_g_scheduler_cv.notify_one();
            }
            continue;
        }

        bool instruction_succeeded = false;
        const std::string& command_str = my_process->commands[my_process->program_counter];
        std::istringstream iss(command_str);
        std::string command;
        iss >> command;
        
        if (command == "READ" || command == "WRITE") {
            std::string var_name, addr_str;
            iss >> (command == "READ" ? var_name : addr_str);
            
            int address;
            if (command == "WRITE") iss >> var_name;

            try {
                address = std::stoi(addr_str, nullptr, 16);
            } catch(...) {
                address = -1;
            }
            
            bool is_write = (command == "WRITE");
            char* physical_ptr = memory_manager->access_memory(*my_process, address, is_write);

            if (my_process->state == ProcessState::TERMINATED) {
                continue;
            }
            
            if (physical_ptr == nullptr) {
                {
                    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                    my_process->state = ProcessState::BLOCKED;
                    rr_g_blocked_queue.push_back(my_process);
                    rr_g_running_processes[core_id] = nullptr;
                    rr_g_scheduler_cv.notify_one();
                }
            } else {
                if (is_write) {
                    uint16_t val = parse_value_or_variable(var_name, *my_process);
                    *(reinterpret_cast<uint16_t*>(physical_ptr)) = val;
                } else {
                    uint16_t val = *(reinterpret_cast<uint16_t*>(physical_ptr));
                    my_process->variables[var_name] = val;
                }
                instruction_succeeded = true;
            }
        } 
        else if (command == "DECLARE") {
            std::string var_name, val_str;
            iss >> var_name >> val_str;
            my_process->variables[var_name] = static_cast<uint16_t>(std::stoi(val_str));
            instruction_succeeded = true;
        }
        else if (command == "ADD" || command == "SUBTRACT") {
            std::string dest_var, src1_str, src2_str;
            iss >> dest_var >> src1_str >> src2_str;
            uint16_t val1 = parse_value_or_variable(src1_str, *my_process);
            uint16_t val2 = parse_value_or_variable(src2_str, *my_process);
            my_process->variables[dest_var] = (command == "ADD") ? (val1 + val2) : (val1 - val2);
            instruction_succeeded = true;
        }
        else if (command == "PRINT") {
             std::string full_arg = command_str.substr(command_str.find('(') + 1);
             full_arg = full_arg.substr(0, full_arg.find_last_of(')'));
             std::string literal = full_arg.substr(0, full_arg.find('+'));
             literal = literal.substr(literal.find('"') + 1, literal.find_last_of('"') - 1);
             std::string var_name = full_arg.substr(full_arg.find('+') + 1);
             var_name.erase(0, var_name.find_first_not_of(" "));

             uint16_t val = my_process->variables[var_name];
             my_process->output_logs.push_back(literal + std::to_string(val));
             instruction_succeeded = true;
        }
        else {
            instruction_succeeded = true;
        }
        
        if (instruction_succeeded) {
            vmstats_increment_active_ticks();
            my_process->program_counter++;
            my_process->commands_executed_this_quantum++;

            if (delayPerExec > 0) {
                 std::this_thread::sleep_for(std::chrono::milliseconds(delayPerExec));
            }

            bool is_finished = (static_cast<size_t>(my_process->program_counter) >= my_process->commands.size());
            bool quantum_expired = (my_process->commands_executed_this_quantum >= qCycles);

            if (is_finished || quantum_expired) {
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                if (is_finished) {
                    my_process->state = ProcessState::FINISHED;
                    my_process->finish_time = std::chrono::system_clock::now();
                    rr_g_finished_processes.push_back(my_process);
                    memory_manager->deallocate_for_process(*my_process);
                } else {
                    my_process->state = ProcessState::READY;
                    rr_g_ready_queue.push_back(my_process);
                }
                rr_g_running_processes[core_id] = nullptr;
                rr_g_scheduler_cv.notify_one();
            }
        }
    }
}

uint16_t parse_value_or_variable(const std::string& token, Process& process) {
    if (process.variables.count(token)) {
        return process.variables[token];
    }
    try {
        return static_cast<uint16_t>(std::stoi(token));
    } catch (...) {
        return 0;
    }
}

void rr_create_processes(MemoryManager& mm) {
    while (!g_is_shutting_down) {
        if (process_maker_running) {
            {
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                std::string name = "process" + std::to_string(cpuClocks.load());
                std::uniform_int_distribution<> mem_dist(MIN_MEM_PER_PROC, MAX_MEM_PER_PROC);
                size_t mem_size = mem_dist(rr_gen);

                g_creation_queue.push_back({name, mem_size, {}});
            }
            rr_g_scheduler_cv.notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(processFrequency));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}