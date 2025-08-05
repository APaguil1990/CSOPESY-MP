/** 
 * CSOPESY Command Line Interface 
*/
#include "ProcessSMI.h"
#include "ScreenManager.h"
#include "vmstat.h"
#include <iostream> 
#include <fstream>
#include <string> 
#include <windows.h> 
#include <vector> 
#include <algorithm> 
#include <thread>
#include <sstream>
#include <atomic>
#include "Process.h"
#include "MemoryManager.h"
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>

#include "global.h"
#include "FCFS.h"
#include "RR.h"
#include "Process.h"
#include "MemoryManager.h"

using namespace std;

std::deque<ProcessCreationRequest> g_creation_queue;

// RR Scheduler Globals
std::deque<std::shared_ptr<Process>> rr_g_ready_queue;
std::vector<std::shared_ptr<Process>> rr_g_running_processes(128, nullptr);
std::vector<std::shared_ptr<Process>> rr_g_finished_processes;
std::deque<std::shared_ptr<Process>> rr_g_blocked_queue;
std::mutex rr_g_process_mutex;
std::condition_variable rr_g_scheduler_cv;
std::atomic<bool> rr_g_is_running(true);

// FCFS Scheduler Globals
std::deque<std::shared_ptr<Process>> fcfs_g_ready_queue;
std::vector<std::shared_ptr<Process>> fcfs_g_running_processes(128, nullptr);
std::vector<std::shared_ptr<Process>> fcfs_g_finished_processes;
std::deque<std::shared_ptr<Process>> fcfs_g_blocked_queue;
std::mutex fcfs_g_process_mutex;
std::condition_variable fcfs_g_scheduler_cv;
std::atomic<bool> fcfs_g_is_running(true);

bool initFlag = false;

bool process_maker_running = false;

//config parameters
int CPU_COUNT = 128; // cpus available [1, 128]
string scheduler = ""; // fcfs or rr
int qCycles = 100000; // quantum [1, 2^32]
int processFrequency = 100000; // every x cycles, generate a new process for scheduler-start [1, 2^32]
int MIN_INS = 1; // min instructions per process [1, 2^32]
int MAX_INS = 1; // max instructions per process [1, 2^32]
int delayPerExec = 100000; // delay between executing next instruction [0, 2^32]

int MAX_OVERALL_MEM = 0;
int MEM_PER_FRAME = 0;
int MEM_PER_PROC = 0;

int FRAME_COUNT = 0;

unsigned short variable_a = 0;
unsigned short variable_b = 0;
unsigned short variable_c = 0;

int cpuClocks = 1;
std::atomic<bool> g_system_initialized = false;
std::mutex g_cout_mutex;
// --- MODIFIED: Changed to a global pointer to be initialized later ---
MemoryManager* memory_manager = nullptr;
// Color definitions 
const int LIGHT_GREEN = 10;     // Light green text 
const int LIGHT_YELLOW = 14;    // Light yellow text 
const int DEFAULT_COLOR = 7;    // Default console color

// Position tracking
SHORT inputLineY = 0;       // Y-coordinate for input prompt line 
SHORT outputLineY = 0;      // Y-coordinate for command output display

// Console handle 
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);  // Windows console handle

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

/**
 * Sets console text color using Windows API 
 * @param color Windows color code (0-15)
*/
void setColor(int color) {
    SetConsoleTextAttribute(hConsole, color); 
} 

void resetColor() {
    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR); 
} 

/**
 * Prints text centered within current console width 
 * @param text String to center-align 
 * @param width Current console width in characters
*/
void printCentered(const string& text, int width) {
    int padding = (width - text.length()) / 2; 
    padding = max(0, padding);
    cout << string(padding, ' ') << text << endl;
}

/**
 * Generates ASCII header with CSOPESY text 
 * @details Uses charPatterns to build letters line-by-line 
 *          Wraps header in asterisk border matching console width 
*/
void printHeader() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1; 
    width = max(width, 80);

    setColor(DEFAULT_COLOR); // Cyan for header
    cout << string(width, '=') << endl; 

    for (const string& line : hardcodedHeader) {
        printCentered(line, width);
    }

    cout << string(width, '=') << endl; 
    resetColor(); 
}

/**
 * Displays welcome messages with color formatting 
 * @details First line: Light green welcome text 
 *          Second line: Light yellow instructions 
*/
void printWelcome() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    setColor(LIGHT_GREEN); 
    cout << "Hello, Welcome to CSOPESY Command Line Interface!\n"; 
    resetColor(); 

    setColor(LIGHT_YELLOW); 
    cout << "Type 'exit' to quit, 'clear' to clear the terminal\n"; 
    resetColor();
}

/**
 * Clears current input line by overwriting with spaces 
 * @details Resets cursor to start of input line after clearing
*/
void clearInputLine() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    COORD inputPos = {0, inputLineY}; 
    SetConsoleCursorPosition(hConsole, inputPos); 
    cout << string(csbi.srWindow.Right, ' '); 
    SetConsoleCursorPosition(hConsole, inputPos);
}

/**
 * Initializes cursor position trackers for i/o lines 
 * @details Sets global Y-coordinates based on current cursor position
*/
void initializePositions() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    inputLineY = csbi.dwCursorPosition.Y; 
    outputLineY = inputLineY + 1;
}

/**
 * Clears screen and reinitializes interface 
 * @details 1. Fills console with spaces 
 *          2. Reprints header/welcome messages 
 *          3. Maintains color consistency 
*/
void clearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 

    COORD topLeft = {0, 0}; 
    DWORD written; 
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written); 

    SetConsoleCursorPosition(hConsole, topLeft); 
    printHeader(); 
    printWelcome();

    CONSOLE_SCREEN_BUFFER_INFO newCsbi; 
    GetConsoleScreenBufferInfo(hConsole, &newCsbi); 
    COORD newPos = {0, static_cast<SHORT>(newCsbi.dwCursorPosition.Y)}; 
    SetConsoleCursorPosition(hConsole, newPos);

    initializePositions();
}

/**
 * Clears command output display line 
 * @details Overwrites output line with spaces while preserving position
*/
void clearOutputLine() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 

    COORD outputPos = {0, outputLineY}; 
    SetConsoleCursorPosition(hConsole, outputPos); 
    cout << string(csbi.srWindow.Right, ' ');
}

/**
 * Splits input string into space-delimited tokens 
 * @param input Raw command string to tokenize 
 * @return Vector of individual command tokens
*/
vector<string> tokenize(const string& input) {
    vector<string> tokens; 
    istringstream iss(input); 
    string token; 

    while (iss >> token) {
        tokens.push_back(token);
    } 
    return tokens;
}

/**
 * Runs the marquee console
 */
void runMarquee(){
    void marquee();
    marquee();
}

/**
 * Runs the FCFS Scheduler
 */
void runFCFS(){
    FCFS();
}

/**
 * Runs the RR Scheduler
 */
void runRR(){
    RR();
}


void fcfs_generate_processes() {
    void fcfs_create_processes(MemoryManager& mm);
    fcfs_create_processes(*memory_manager);
}

void rr_searchTest(std::string processName) {
    // void rr_search_process(std::string processName);
    // rr_search_process(processName);
}

void rr_displayTest(){
    void rr_display_processes();
    rr_display_processes();
}

void rr_writeTest(){
    void rr_write_processes();
    rr_write_processes();
}

void fcfs_displayTest(){
    void fcfs_display_processes();
    fcfs_display_processes();
}

void fcfs_writeTest(){
    void fcfs_write_processes();
    fcfs_write_processes();
}


/**
 * Checks for the config.txt file and reads its contents to get values
 * @return true or false
 */
bool readConfig(){
    vector<string> values;

    ifstream configFile("config.txt");
            
    if(!configFile.is_open()){
        return false;
    }

    string line;
    while(getline(configFile,line)){
        istringstream iss(line);
        string key, value;
        // Read key and value from the line
        if (std::getline(iss, key, ' ') && std::getline(iss, value)) {
            // Remove quotes from the value if present
            if (value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            
            // Assign values to corresponding variables based on the key
            if (key == "num-cpu") {
                CPU_COUNT = std::stoi(value);
            } else if (key == "scheduler") {
                scheduler = value;
            } else if (key == "quantum-cycles") {
                qCycles = std::stoi(value);
            } else if (key == "min-ins") {
                MIN_INS = std::stoi(value);
            } else if (key == "max-ins") {
                MAX_INS = std::stoi(value);
            } else if (key == "delay-per-exec") {
                delayPerExec = std::stoi(value);
            } else if (key == "max-overall-mem") {
                MAX_OVERALL_MEM = std::stoi(value);
            } else if (key == "mem-per-frame") {
                MEM_PER_FRAME = std::stoi(value);
            } else if (key == "mem-per-proc") {
                MEM_PER_PROC = std::stoi(value);
            }
        }
    }
    configFile.close();
    initFlag = true;
    return true;
}

// Validates memory size 
bool isValidMemorySize(size_t size) {
    if (size < 64 || size > 65536) return false; 
    // Check if size is power of 2 
    return (size & (size - 1)) == 0; 
}

bool vmStat(){
    cout << "\n";
    cout << "Total memory     : " << get_total_memory() << " bytes\n";
    cout << "Used memory      : " << memory_manager->get_used_memory_bytes() << " bytes\n";
    cout << "Free memory      : " << memory_manager->get_free_memory_bytes() << " bytes\n";
    cout << "Idle CPU ticks   : " << get_idle_cpu_ticks() << "\n";
    cout << "Active CPU ticks : " << get_active_cpu_ticks() << "\n";
    cout << "Total CPU ticks  : " << get_total_cpu_ticks() << "\n";
    cout << "Num paged in     : " << memory_manager->pages_paged_in << "\n";
    cout << "Num paged out    : " << memory_manager->pages_paged_out << "\n";
    return true;
}

/**
 * Processes user commands and return response 
 * @param cmd Input command string 
 * @return Response messages or empty string for clear 
 * @details Valid commands: initialize, screen, scheduler-test, 
 *                          scheduler-stop, report-util, clear, exit
*/
string processCommand(const string& cmd) {
    auto manager = ScreenManager::getInstance(); 
    vector<string> tokens = tokenize(cmd);
    
    vector<string> validCommands = {
        "initialize", "screen", "scheduler-start", "marquee",
        "scheduler-stop", "report-util", "vmstat", "process-smi", "clear", "exit"
    };

    //handles empty inputs
    if (cmd.empty()){
        return "No input provided";
    }

    //handle initialization before other commands
    if (find(validCommands.begin(), validCommands.end(), cmd) != validCommands.end() && !initFlag){
         if (cmd == "initialize"){
            if (initFlag) return "Already initialized.";

            if (readConfig() == false){
                return "initialization failed, please try again";
            };

            // --- CRITICAL FIX: Create the MemoryManager HERE ---
            if (memory_manager == nullptr) {
                memory_manager = new MemoryManager(rr_g_ready_queue, rr_g_running_processes, fcfs_g_ready_queue, fcfs_g_running_processes);
            }
            
            // Set the flag to true, main() will now launch the threads.
            initFlag = true; 
            g_system_initialized = true;

            return "Initialization finished. Running '" + scheduler + "' scheduler.";
        }

        //can use the exit command
        if (cmd == "exit") exit(0);
        return "use the 'initialize' command before using other commands";
    }

    // Handle screen commands 
    if (tokens[0] == "screen" && initFlag == true) {
        if (tokens.size() >= 4 &&  tokens[1] == "-s") {
            if (process_maker_running) {
                try {
                    size_t mem_size = stoull(tokens[3]); 

                    if (!isValidMemorySize(mem_size)) {
                        return "Invalid memory allocation: must be power of 2 between 64-65536 bytes.";
                    } 
                    manager->createScreen(tokens[2]); 

                   {
                        // Lock the mutex to safely access the shared g_creation_queue
                        std::lock_guard<std::mutex> lock(rr_g_process_mutex);
                        // Add the new process information to the queue
                        g_creation_queue.push_back({tokens[2], mem_size});
                    }
                    // Notify the sleeping scheduler thread that there is a new request for it to handle
                    rr_g_scheduler_cv.notify_one();
                    
                    return "Request to create process '" + tokens[2] + "' submitted.";
                } catch(...) {
                    return "Invalid memory size format.";
                }
            } else {
                return "scheduler has not been started yet!";
            }
        } else if (tokens.size() >= 3 && tokens[1] == "-r") { // Added size check for safety
            if (manager->screenExists(tokens[2])) {
                manager->attachScreen(tokens[2]); 
                rr_searchTest(tokens[2]);
                return "";
            } else {
                return "Screen not found: " + tokens[2]; 
            }
        } else if (tokens.size() >= 2 && tokens[1] == "-ls") { // Added size check for safety
            if (scheduler == "fcfs") {
                fcfs_displayTest();
            } else if (scheduler == "rr") {
                rr_displayTest();
            }
            return "";
        }else if (tokens.size() >= 2 && tokens[1] == "-c") { // Added size check for safety
            //TODO: Ability to add a set of user-defined instructions when creating a process.
            return "screen -c not yet implemented.";
        } else {
            return "Invalid 'screen' command syntax.";
        }
    }else if (tokens[0] == "screen" && initFlag == false){
        return "use the 'initialize' command before using other commands";
    }

    // Handle quit 
    if (manager->screenActive() && (tokens[0] == "quit" || tokens[0] == "exit")) {
        manager->detachScreen(); 
        clearScreen(); 
        initializePositions(); 
        return "Returned to main menu";
    }

    if (find(validCommands.begin(), validCommands.end(), cmd) != validCommands.end() && initFlag == true) {
        if (cmd == "clear") {
            clearScreen();
            return "SCREEN CLEARED"; // Special flag
        }
        
        if (cmd == "marquee"){
            thread marquee(runMarquee);
            marquee.join();
            clearScreen();
            return "marquee console finished";
        }

        if (cmd == "scheduler-start"){
            if (scheduler == "fcfs"){
               // Using a lambda to safely call the function in a new thread
               thread process_generator_fcfs([](){
                   // The code inside this lambda runs in the new thread.
                   void fcfs_create_processes(MemoryManager& mm); // Forward declare
                   fcfs_create_processes(*memory_manager);      // Call with dereferenced pointer
               });
               process_generator_fcfs.detach();
               return "running FCFS scheduler process generator";
           }else if (scheduler == "rr"){
               // Using a lambda for the RR scheduler as well for safety and consistency
               thread process_generator_rr([](){
                   // The code inside this lambda runs in the new thread.
                   void rr_create_processes(MemoryManager& mm); // Forward declare
                   rr_create_processes(*memory_manager);      // Call with dereferenced pointer
               });
               process_generator_rr.detach();
               return "running RR scheduler process generator";
            }
            return "error: cannot define scheduler";
        }

        if (cmd == "scheduler-stop"){
            process_maker_running = false;
            return "scheduler stopped";
        }

        if (cmd == "initialize"){
            return "initialize has already been used";
        }

        if (cmd == "vmstat"){
            if(vmStat() == false){
                return "error: cannot retrieve information for vmstat"; 
            }
            return "";
        }

        if (cmd == "process-smi"){
            std::cout << '\n'; 
            process_smi::printSnapshot(); 
            std::cout << std::endl; 
            return "";
        }

        if (cmd == "report-util"){
            if (scheduler == "rr") {
                rr_writeTest();
            } else if (scheduler == "fcfs") {
                fcfs_writeTest();
            }
            return "Report written to csopesy-log.txt";
        }

        if (cmd == "exit") exit(0);
    }
    
    return "Unknown command: " + cmd;
}

/**
 * Program entry point and main loop 
 * @details Handles screen initialization, command processing, and UI updates
*/
int main() {
    auto screen_manager = ScreenManager::getInstance();
    clearScreen();

    // --- Thread handle for the main scheduler ---
    std::thread scheduler_thread;
    bool scheduler_running = false;

    string input;
    while (true) {
        initializePositions();
        
        // --- The main input loop ---
        if (!screen_manager->screenActive()) {
            cout << "root:\\> ";
        } else {
            cout << "Screen active (type 'quit' to return): ";
        }

        if (!getline(cin, input)) { break; } // Exit on input failure
        
        // --- CRITICAL CHANGE: Initialization is handled first ---
        if (!initFlag && input == "initialize") {
            cout << "Initializing..." << endl;
            if (readConfig()) {
                // --- Create the Memory Manager HERE, and ONLY here ---
                if (memory_manager == nullptr) {
                    memory_manager = new MemoryManager(rr_g_ready_queue, rr_g_running_processes, fcfs_g_ready_queue, fcfs_g_running_processes);
                }
                initFlag = true; // Mark initialization as complete
                cout << "Initialization successful. Scheduler: " << scheduler << endl;

                // --- Launch the main scheduler thread AFTER manager is created ---
                if (!scheduler_running) {
                    if (scheduler == "rr") {
                        scheduler_thread = std::thread(RR);
                        scheduler_thread.detach();
                    } else if (scheduler == "fcfs") {
                        scheduler_thread = std::thread(FCFS);
                        scheduler_thread.detach();
                    }
                    scheduler_running = true;
                }
            } else {
                cout << "Initialization failed. Check config.txt." << endl;
            }
            continue; // Go to the next loop iteration to get a new command
        }

        // --- All other commands are processed here ---
        string cmd_output = processCommand(input);
        
        if (input == "exit") {
            break;
        }

        if (!cmd_output.empty()) {
            // ... (your existing output logic is fine)
        }
    }

    // --- Final Shutdown ---
    cout << "Shutting down..." << endl;
    process_maker_running = false; // Signal generator to stop
    rr_g_is_running = false;
    fcfs_g_is_running = false;
    rr_g_scheduler_cv.notify_all();
    fcfs_g_scheduler_cv.notify_all();
    
    // Give detached threads a moment to clean up
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    delete memory_manager; // Clean up the allocated memory
    cout << "\nProgram finished." << endl;
    return 0;
}