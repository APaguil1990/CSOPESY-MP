#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ostream>
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
#include <unordered_set>

#include "RR.h"
#include "ScreenManager.h"
#include "config.h"
#include "vmstat.h"

std::mutex g_cout_mutex;
// --- Configuration ---
// const int NUM_PROCESSES = 10;
// const int COMMANDS_PER_PROCESS = 105;

// --- Process State ---
// enum class ProcessState {
//     READY,
//     RUNNING,
//     FINISHED
// };

// struct frame {
//     int max_memory = MEM_PER_FRAME; // cant get declared properly here because of how the initialize commands work, needs a check
//     std::vector<int> current_memory; // this should be a vector with another one for wat process is in here
//     std::vector<int> current_occupied;
// };

// --- Process Control Block (PCB) ---
// struct RR_PCB { 
//     int id;
//     std::string processName = "";
//     int commands_executed_this_quantum;
//     ProcessState state;
//     std::vector<std::string> commands;
//     size_t program_counter = 0;
//     std::chrono::system_clock::time_point start_time;
//     std::chrono::system_clock::time_point finish_time;
//     int assigned_core = -1;
//     std::vector<std::string> log_file;

//     // std::ofstream log_file;
//     // RR_PCB(int pid) : id(pid), state(ProcessState::READY) {
//     //     std::stringstream ss;
//     //     ss << "process" << (id < 10 ? "0" : "") << id << ".txt";
//     //     log_file.open(ss.str());
//     // }

//     // ~RR_PCB() {
//     //     if (log_file.is_open()) {
//     //         log_file.close();
//     //     }
//     // }
// };

// std::vector<std::shared_ptr<frame>> rr_memory_block(2048, nullptr); // unknown max possible frames? maybe a different data struct can handle this better
int memoryCycle = 0;

std::unordered_set<std::shared_ptr<RR_PCB>> rr_g_memory_processes;

std::random_device rr_rd;  // a seed source for the random number engine
std::mt19937 rr_gen(rr_rd()); // mersenne_twister_engine seeded with rd()

// --- Shared Data Structures ---
std::deque<std::shared_ptr<RR_PCB>> rr_g_ready_queue;
std::vector<std::shared_ptr<RR_PCB>> rr_g_running_processes(128, nullptr); // 128 is max cpu count
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

std::vector<std::string> rr_getRunningProcessNames() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex); 
    std::vector<std::string> out; 

    for (const auto& p : rr_g_running_processes) {
        if (p) out.emplace_back(p->processName);
    } 
    return out;
}

void rr_declareCommand(std::shared_ptr<RR_PCB> process, std::string varA, uint16_t value) {
    // Check if variable already exists
    bool found = false;
    for (auto& [name, val] : process->variables) {
        if (name == varA) {
            val = value;  // Update existing value
            found = true;
            break;
        }
    }
    
    // If variable doesn't exist, add it
    if (!found) {
        process->variables.emplace_back(varA, value);
    }
}

// void display_memory() {
//     int i = 4;
    
//     compute_used_memory(rr_g_memory_processes.size());
//     std::ostringstream filename;
//     filename << "memory_stamp_" << memoryCycle << ".txt";

//     std::ofstream memory_file (filename.str());

//     memory_file << "Timestamp: (" << rr_format_time(std::chrono::system_clock::now(), "%m/%d/%Y %I:%M:%S%p") << ")" << std::endl;
//     memory_file << "Number of processes in memory: " << rr_g_memory_processes.size() << std::endl;
//     memory_file << "Total external fragmentation in KB: " << (i-rr_g_memory_processes.size())*MEM_PER_PROC << std::endl << std::endl;

//     memory_file << "----end---- = " << MAX_OVERALL_MEM << std::endl << std::endl;

//     for (const auto& p : rr_g_memory_processes) {
//         if (p) {
//             memory_file << MEM_PER_PROC*i << std::endl;
//             memory_file << p->processName << std::endl;
//             i--;
//             memory_file << MEM_PER_PROC*i << std::endl << std::endl;
//         }
//     }

//     memory_file << "----start---- = 0" << std::endl;

//     memory_file.close();
// }

void rr_addCommand(std::shared_ptr<RR_PCB> process, std::string varA, std::string varB, std::string varC) {
    uint16_t valB = 0;
    uint16_t valC = 0;
    
    for (auto& [name, value] : process->variables) {
        if (name == varB) {
            valB = value;
            break;
        }
    }
    
    for (auto& [name, value] : process->variables) {
        if (name == varC) {
            valC = value;
            break;
        }
    }
    
    uint16_t sum = valB + valC;
    
    bool found = false;
    for (auto& [name, value] : process->variables) {
        if (name == varA) {
            value = sum;
            found = true;
            break;
        }
    }
    
    if (!found) {
        process->variables.emplace_back(varA, sum);
    }
}

void rr_subtractCommand(std::shared_ptr<RR_PCB> process, std::string varA, std::string varB, std::string varC) {
    uint16_t valB = 0;
    uint16_t valC = 0;
    
    for (auto& [name, value] : process->variables) {
        if (name == varB) {
            valB = value;
            break;
        }
    }
    
    for (auto& [name, value] : process->variables) {
        if (name == varC) {
            valC = value;
            break;
        }
    }
    
    uint16_t sum = valB - valC;
    
    bool found = false;
    for (auto& [name, value] : process->variables) {
        if (name == varA) {
            value = sum;
            found = true;
            break;
        }
    }
    
    if (!found) {
        process->variables.emplace_back(varA, sum);
    }
}

void rr_sleepCommand() {
    // cpuClocks += 10;
}

void rr_forCommand() {
    // for (int i; i < 5; i++) {
    //     rr_addCommand();
    // }
}

void rr_readCommand(std::shared_ptr<RR_PCB> process, std::string varA, std::string memory) {
    uint16_t mem_value = 0;
    
    // Find the value in memory_variables
    for (auto& [name, value] : memory_variables) {
        if (name == memory) {
            mem_value = value;
            break;
        }
    }

    // Update the variable in process->variables
    bool found = false;
    for (auto& [name, value] : process->variables) {
        if (name == varA) {
            value = mem_value;  // Write the memory value to the variable
            found = true;
            break;
        }
    }
    
    // If varA doesn't exist in process->variables, add it
    if (!found) {
        process->variables.emplace_back(varA, mem_value);
    }
}

void rr_writeCommand(std::shared_ptr<RR_PCB> process, std::string memory, std::string varA) {
    uint16_t mem_value = 0;
    
    // Find the value in memory_variables
    for (auto& [name, value] : process->variables) {
        if (name == varA) {
            mem_value = value;
            break;
        }
    }

    // Update the variable in process->variables
    bool found = false;
    for (auto& [name, value] : memory_variables) {
        if (name == memory) {
            value = mem_value;  // Write the memory value to the variable
            found = true;
            break;
        }
    }

    if (!found) {
        process->variables.emplace_back(varA, mem_value);
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

        for (int i = 0; i < CPU_COUNT; ++i) { // check if empty slot available and ready queue available
            if (rr_g_running_processes[i] == nullptr && !rr_g_ready_queue.empty()) {
                std::shared_ptr<RR_PCB> process = rr_g_ready_queue.front();
                
                // Check if memory has space or if the process is already in memory
                if (rr_g_memory_processes.size() < FRAME_COUNT || rr_g_memory_processes.find(process) != rr_g_memory_processes.end()) {
                    
                    // Add to memory if not already present
                    if (rr_g_memory_processes.find(process) == rr_g_memory_processes.end()) {
                        rr_g_memory_processes.insert(process);
                        vmstats_increment_paged_in();
                    }
                    
                    // Schedule the process
                    rr_g_ready_queue.pop_front();
                    process->state = ProcessState::RUNNING;
                    process->assigned_core = i;
                    process->commands_executed_this_quantum = 0;
                    rr_g_running_processes[i] = process;
                } else {
                    rr_g_ready_queue.pop_front();
                    rr_g_ready_queue.emplace_back(process);
                }
                // If memory is full and process isn't in memory, leave it in the queue
            }
        }
    }
}

std::vector<std::string> rr_tokenize(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    return tokens;
}

std::vector<std::string> rr_tokenizePrintStatement(const std::string& input) {
    std::vector<std::string> tokens;
    
    // Find the positions of key elements
    size_t openParen = input.find('(');
    size_t closeParen = input.rfind(')');
    
    // Check if the format is correct
    if (openParen == std::string::npos || closeParen == std::string::npos || 
        openParen >= closeParen) {
        return tokens; // return empty vector if format is invalid
    }
    
    // Extract the content inside parentheses
    std::string content = input.substr(openParen + 1, closeParen - openParen - 1);
    
    // Find the positions of the quote marks
    size_t firstQuote = content.find('"');
    size_t secondQuote = content.rfind('"');
    
    // Check if there are quoted strings
    if (firstQuote != std::string::npos && secondQuote != std::string::npos && 
        firstQuote < secondQuote) {
        // Extract the quoted text
        std::string quotedText = content.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        tokens.push_back(quotedText);
        
        // Extract the remaining part after the quoted text
        std::string remaining = content.substr(secondQuote + 1);
        
        // Remove any + signs and whitespace
        remaining.erase(std::remove(remaining.begin(), remaining.end(), '+'), remaining.end());
        remaining.erase(std::remove(remaining.begin(), remaining.end(), ' '), remaining.end());
        
        if (!remaining.empty()) {
            tokens.push_back(remaining);
        }
    } else {
        // If no quotes, just process the entire content
        std::string cleaned = content;
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '+'), cleaned.end());
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ' '), cleaned.end());
        
        if (!cleaned.empty()) {
            tokens.push_back(cleaned);
        }
    }
    
    return tokens;
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

                vmstats_increment_active_ticks();

                std::vector<std::string> words = rr_tokenize(command);

                if (words[0].compare("declare") == 0) {
                    uint16_t value = static_cast<uint16_t>(std::stoi(words[2]));
                    rr_declareCommand(my_process, words[1], value);
                    
                } else if (words[0].compare("add") == 0) {
                    rr_addCommand(my_process, words[1], words[2], words[3]);

                } else if (words[0].compare("sub") == 0) {
                    rr_subtractCommand(my_process, words[1], words[2], words[3]);

                } else if (words[0].compare("sleep") == 0) {
                    rr_sleepCommand();

                } else if (words[0].compare("for") == 0) {
                    rr_forCommand();

                } else if (words[0].compare("read") == 0) {
                    rr_readCommand(my_process, words[1], words[2]);

                } else if (words[0].compare("write") == 0) {
                    rr_writeCommand(my_process, words[1], words[2]);

                } else {
                    // log file in memory
                    // std::ostringstream tempString;
                    // tempString << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                    //            << " \"" << command << "\"" << std::endl;
                    // my_process->log_file.push_back(tempString.str());

                    std::ostringstream tempString;
                    // tempString << std::get<0>(my_process->variables[0]) << std::get<1>(my_process->variables[0]) << std::endl << std::get<0>(my_process->variables[1]) << std::get<1>(my_process->variables[1]) << std::endl << std::get<0>(my_process->variables[2]) << std::get<1>(my_process->variables[2]) << std::endl;
                    // my_process->log_file.push_back(tempString.str());

                    std::vector<std::string> tokens = rr_tokenizePrintStatement(command);


                    for (auto& token : tokens) {
                        bool found = false;
                        for (auto& [name, value] : my_process->variables) {
                            if (token == name) {
                                tempString << value;
                                found = true;
                                break;
                            }
                        }
                        if (found == false) {
                            tempString << token;
                        }
                    }

                    // tempString << tokens[0];
                    // tempString << std::endl;
                    // tempString << tokens[1];
                    // tempString << std::endl;
                    my_process->log_file.push_back(tempString.str());

                    // log file in file
                    // const std::string& command = my_process->commands[my_process->program_counter];
                    // my_process->log_file << "(" << rr_format_time(now, "%m/%d/%Y %I:%M:%S%p") << ") Core:" << core_id
                    //                  << " \"" << command << "\"" << std::endl;
                }



                my_process->program_counter++;
                my_process->commands_executed_this_quantum++;

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            std::unique_lock<std::mutex> lock(rr_g_process_mutex);
            if (my_process->commands_executed_this_quantum >= qCycles && my_process->program_counter < my_process->commands.size()) {
                // Preempt the process and put it back in the ready queue
                my_process->state = ProcessState::READY;
                rr_g_ready_queue.push_back(my_process);
                rr_g_running_processes[core_id] = nullptr;
                rr_g_scheduler_cv.notify_one();

                // printing goes here
                // display_memory();
                memoryCycle++;
            } 
            else if (my_process->program_counter >= my_process->commands.size()) { // edit here for memory allocator
                // Process has completed all commands
                my_process->state = ProcessState::FINISHED;
                my_process->finish_time = std::chrono::system_clock::now();
                rr_g_finished_processes.push_back(my_process);
                rr_g_running_processes[core_id] = nullptr;

                rr_g_memory_processes.erase(my_process);
                vmstats_increment_paged_out();
                
                rr_g_scheduler_cv.notify_one();
            }
            lock.unlock();

        } else {
            vmstats_increment_idle_ticks();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// find process for screen -r (log file in memory)
void rr_search_process(std::string process_search) {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
    std::stringstream tempString;
    std::vector<std::shared_ptr<RR_PCB>> search_vector;
    std::shared_ptr<RR_PCB> process = nullptr;

    std::cout << "\n-------------------------------------------------------------\n";

    // Check if any container has processes
    bool has_processes = !rr_g_ready_queue.empty() || 
                        !rr_g_finished_processes.empty() ||
                        std::any_of(rr_g_running_processes.begin(), 
                                   rr_g_running_processes.end(),
                                   [](const auto& p) { return p != nullptr; });

    if (has_processes) {
        // Add non-null processes from each container
        for (const auto& p : rr_g_ready_queue) {
            if (p) search_vector.push_back(p);
        }
        
        for (const auto& p : rr_g_running_processes) {
            if (p) search_vector.push_back(p);
        }
        
        for (const auto& p : rr_g_finished_processes) {
            if (p) search_vector.push_back(p);
        }

        // Search for the process
        for (const auto& p : search_vector) {
            if (p && process_search == p->processName) {
                process = p;
                break;
            }
        }

        if (process != nullptr) {
            for(const std::string& line : process->log_file) {
                std::cout << line << std::endl;
            }
        } else {
            std::cout << "Process not found" << std::endl;
        }
    } else {
        std::cout << "Currently no processes running or waiting to run";
    }

    std::cout << "\n-------------------------------------------------------------\n";
}

// --- UI Function to Display Process Lists ---
void rr_display_processes() {
    std::lock_guard<std::mutex> lock(rr_g_process_mutex);
    std::cout << "\n-------------------------------------------------------------\n";

    int busyCores = 0;
    for (const auto& p : rr_g_running_processes) if (p) ++busyCores;
    int cpuUtil   = CPU_COUNT ? static_cast<int>(100.0 * busyCores / CPU_COUNT) : 0;
    std::cout << "CPU Utilization: "  << cpuUtil << "%" << std::endl;

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

void rr_create_process(std::string processName, std::size_t memory_size) {
    // Create a new process
    std::shared_ptr<RR_PCB> pcb;
    {
        std::lock_guard<std::mutex> lock(rr_g_process_mutex);
        
        // pcb = std::make_shared<RR_PCB>(cpuClocks); 
        pcb = std::make_shared<RR_PCB>(cpuClocks);
        pcb->start_time = std::chrono::system_clock::now();
        pcb->processName = processName; 
        pcb->memory_size = memory_size;

        std::uniform_int_distribution<> instructionCount_rand(MIN_INS, MAX_INS);
        std::uniform_int_distribution<> instruction_rand(0, 5);

        int instructionCount = instructionCount_rand(rr_gen);

        for (int j = 0; j < instructionCount; ++j) {
            std::stringstream rr_command_stream;
            int instruction = instruction_rand(rr_gen);
            // int instruction = 0;

            switch (instruction) {
                case 0: // print
                    rr_command_stream << "Hello world from process " << pcb->processName << "!";
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
        }
        cpuClocks++;
        
        rr_g_ready_queue.push_back(pcb);
    }
    
    // Notify scheduler that a new process is available
    rr_g_scheduler_cv.notify_one();
}

void rr_create_process_with_commands(std::string processName, size_t memory_size, const std::vector<std::string>& commands) {
    // Create a new process
    std::shared_ptr<RR_PCB> pcb;
    
    pcb = std::make_shared<RR_PCB>(cpuClocks);
    pcb->start_time = std::chrono::system_clock::now(); 
    pcb->processName = processName;
    pcb->memory_size = memory_size;

    pcb->commands = commands;

    cpuClocks++;
    rr_g_ready_queue.push_back(pcb);

    // Notify scheduler that a new process is available
    rr_g_scheduler_cv.notify_one();
}

// Function that creates processes
void rr_create_processes() {
    process_maker_running = true;
    auto rr_manager = ScreenManager::getInstance();

    // Process Generation Loop
    while (rr_g_is_running) {
        if (process_maker_running) {
            // Create a new process
            std::shared_ptr<RR_PCB> pcb;
            {
                std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                
                // pcb = std::make_shared<RR_PCB>(cpuClocks); 
                pcb = std::make_shared<RR_PCB>(cpuClocks);
                pcb->start_time = std::chrono::system_clock::now(); 
                pcb->memory_size = 64; // Default size

                std::stringstream tempString;
                tempString << "process" << pcb->id;

                pcb->processName = tempString.str();

                std::uniform_int_distribution<> instructionCount_rand(MIN_INS, MAX_INS);
                std::uniform_int_distribution<> instruction_rand(0, 5);

                int instructionCount = instructionCount_rand(rr_gen);

                for (int j = 0; j < instructionCount; ++j) {
                    std::stringstream rr_command_stream;
                    int instruction = instruction_rand(rr_gen);

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
                }
                
                cpuClocks++;
                rr_g_ready_queue.push_back(pcb);
            }
            
            // Notify scheduler that a new process is available
            rr_g_scheduler_cv.notify_one();
        }

        // Add some delay between process creation if needed
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Check if we should stop
        if (rr_g_ready_queue.empty() && rr_g_running_processes.empty()) {
            rr_g_is_running = false;
            rr_g_scheduler_cv.notify_all();
        }
    }
}

// Function that starts and runs the processes
int RR() {
    // Create scheduler and worker threads
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