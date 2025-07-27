/** 
 * CSOPESY Command Line Interface 
*/
#include "ProcessSMI.h"
#include "ScreenManager.h"
#include <iostream> 
#include <fstream>
#include <string> 
#include <windows.h> 
#include <vector> 
#include <algorithm> 
#include <thread>
#include <sstream>

using namespace std;

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
    void FCFS();
    FCFS();
}

/**
 * Runs the RR Scheduler
 */
void runRR(){
    void RR();
    RR();
}

void rr_generate_processes() {
    void rr_create_processes();
    rr_create_processes();
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

// Modifed: Added memory_size parameter
void rr_nameProcess(std::string processName, size_t memory_size) {
    void rr_create_process(std::string processName, size_t memory_size);
    rr_create_process(processName, memory_size);
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

    FRAME_COUNT = MAX_OVERALL_MEM / MEM_PER_PROC;

    initFlag = true;
    return true;
}

// Validates memory size 
bool isValidMemorySize(size_t size) {
    if (size < 64 || size > 65536) return false; 

    // Check if size is power of 2 
    return (size & (size - 1)) == 0; 
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
    if (find(validCommands.begin(), validCommands.end(), cmd) != validCommands.end() && initFlag == false){
        if (cmd == "initialize"){
            //read the config file
            if (readConfig() == false){
                return "initialization failed, please try again";
            };

            if (scheduler == "fcfs"){
               thread schedulerFCFS(runFCFS);
               schedulerFCFS.detach();
               return "running FCFS scheduler";
            }else if (scheduler == "rr"){
               thread schedulerRR(runRR);
               schedulerRR.detach();
               return "running RR scheduler";
            }

            return "initialization finished";
        }

        //can use the exit command
        if (cmd == "exit") exit(0);
        return "use the 'initialize' command before using other commands";
    }

    // Handle screen commands 
    if (tokens[0] == "screen" && initFlag == true) {
        // Modified: Added memory size handling for screen -s
        if (tokens.size() >= 4 &&  tokens[1] == "-s") {
            if (process_maker_running) {
                try {
                    size_t mem_size = stoull(tokens[3]); 

                    if (!isValidMemorySize(mem_size)) {
                        return "Invalid memory allocation: must be power of 2 between 64-65536 bytes.";
                    } 
                    manager->createScreen(tokens[2]); 
                    // Moddified: Pass memory size to process creation 
                    rr_nameProcess(tokens[2], mem_size); 
                    return "Created process: " + tokens[2] + " with " + tokens[3] + " bytes";
                } catch(...) {
                    return "Invalid memory size format.";
                }
                // manager->createScreen(tokens[2]); 
                // rr_nameProcess(tokens[2]);
                // return "Created screen: " + tokens[2];
            } else {
                return "scheduler has not been started yet!";
            }
        } else if (tokens[1] == "-r") {
            if (manager->screenExists(tokens[2])) {
                manager->attachScreen(tokens[2]); 
                rr_searchTest(tokens[2]);
                return "";
            } else {
                return "Screen not found: " + tokens[2]; 
            }
        } else if (tokens[1] == "-ls") {
            if (scheduler == "fcfs") {
                fcfs_displayTest();
            } else if (scheduler == "rr") {
                rr_displayTest();
            }
            return "";
        }else if (tokens[1] == "-c") {
            //TODO: Ability to add a set of user-defined instructions when creating a process.
            // Look for process

            // Add instructions to process to be executed
            
            return "";
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
            //start the scheduler thread
            if (scheduler == "fcfs"){
               thread schedulerFCFS(runFCFS);
               schedulerFCFS.detach();
               return "running FCFS scheduler";
            }else if (scheduler == "rr"){
               thread process_generator_rr(rr_generate_processes);
               process_generator_rr.detach();
               return "running RR scheduler";
            }
            //if scheduler does not work
            return "error: cannot define scheduler";
        }

        if (cmd == "scheduler-stop"){
            //stop the scheduler thread
            process_maker_running = false;

            return "scheduler stopped";
        }

        if (cmd == "initialize"){
            return "initialize has already been used";
        }

        if (cmd == "vmstat"){
            //TODO: add function to view a detailed view of the active/inactive processes, available/used memory, and pages.
            
        }

        if (cmd == "process-smi"){
            //TODO: add function to provide a summarized view of the available/used memory, as well as the list of processes and memory occupied. This is similar to the “nvidia-smi” command.
            std::thread snap(process_smi::printSnapshot); 
            snap.detach(); 
            return "";
        }

        if (cmd == "report-util"){
            if (scheduler == "rr") {
                rr_writeTest();
            } else if (scheduler == "fcfs") {
                fcfs_writeTest();
            }
            
        }

        if (cmd == "exit") exit(0);
    }
    //If command was not recognized

    return "Unknown command: " + cmd;
}

/**
 * Program entry point and main loop 
 * @details Handles screen initialization, command processing, and UI updates
*/
int main() {

    auto manager = ScreenManager::getInstance();
    clearScreen();

    string input;
    while (true) {
        // Get current positions
        initializePositions();
        
        if (!manager->screenActive()) {
            // Main screen
            clearInputLine();
            COORD inputPos = {0, inputLineY};
            SetConsoleCursorPosition(hConsole, inputPos);
            cout << "Enter a command: ";
            
            getline(cin, input);
            transform(input.begin(), input.end(), input.begin(), ::tolower);
            
            // Clear input line
            SetConsoleCursorPosition(hConsole, inputPos);
            cout << string(80, ' ');
        } else {
            // Process screen
            clearInputLine();
            COORD inputPos = {0, inputLineY};
            SetConsoleCursorPosition(hConsole, inputPos);
            cout << "Screen active (type 'quit' to return): ";
            
            getline(cin, input);
            transform(input.begin(), input.end(), input.begin(), ::tolower);
            
            // Clear input line
            SetConsoleCursorPosition(hConsole, inputPos);
            cout << string(80, ' ');
        }
        
        string output = processCommand(input);
        
        if (!output.empty()) {
            // Display output at fixed position
            clearOutputLine();
            COORD outputPos = {0, outputLineY};
            SetConsoleCursorPosition(hConsole, outputPos);
            cout << output;
            
            // Return cursor to input position
            COORD inputPos = {0, inputLineY};
            SetConsoleCursorPosition(hConsole, inputPos);
        }
    }
    return 0;
}