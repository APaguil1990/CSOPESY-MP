/** 
 * CSOPESY Command Line Interface 
*/
#include "ScreenManager.h"
#include <iostream> 
#include <string> 
#include <windows.h> 
#include <vector> 
#include <algorithm> 
#include <thread>
#include <sstream>

using namespace std; 

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

void runMarquee(){
    extern void marquee();
    marquee();
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

    // Handle screen commands 
    if (tokens.size() >=3 && tokens[0] == "screen") {
        if (tokens[1] == "-s") {
            manager->createScreen(tokens[2]); 
            return "Created screen: " + tokens[2]; 
        } else if (tokens[1] == "-r") {
            if (manager->screenExists(tokens[2])) {
                manager->attachScreen(tokens[2]); 
                return "";
            }
            return "Screen not found: " + tokens[2]; 
        }
    }

    if (tokens[0] == "marquee"){
        thread marquee(runMarquee);
        marquee.join();
        clearScreen();
    }

    if (tokens[0] == "scheduler-start"){
        
    }

    // Handle quit 
    if (manager->screenActive() && (tokens[0] == "quit" || tokens[0] == "exit")) {
        manager->detachScreen(); 
        clearScreen(); 
        initializePositions(); 
        return "Returned to main menu";
    }
    
    vector<string> validCommands = {
        "initialize", "screen", "scheduler-start", "marquee",
        "scheduler-stop", "report-util", "clear", "exit"
    };
    
    if (find(validCommands.begin(), validCommands.end(), cmd) != validCommands.end()) {
        if (cmd == "clear") {
            clearScreen();
            return "SCREEN CLEARED"; // Special flag
        }
        if (cmd == "exit") exit(0);
        return "'" + cmd + "' command recognized. Doing something.";
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