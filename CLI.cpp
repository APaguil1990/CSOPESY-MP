#include <iostream> 
#include <string> 
#include <windows.h> 
#include <vector> 
#include <algorithm> 

using namespace std; 

// Color definitions 
const int LIGHT_GREEN = 10; 
const int LIGHT_YELLOW = 14; 
const int DEFAULT_COLOR = 7; 

// Console handle 
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 

void setColor(int color) {
    SetConsoleTextAttribute(hConsole, color); 
} 

void resetColor() {
    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR); 
} 

void printHeader() {
    cout << "*******************************************\n";
    cout << "*            _   ___  ___ ___  ___ _      *\n";
    cout << "*   ___ ___| |_| . \\| . | . \\|_ _| |_    *\n";
    cout << "*  / -_|_-<  _| .  /|   |  _/ | ||  _|   *\n";
    cout << "*  \\___/__/\\__|_|\\_\\|_|_|_|  |___|\\__|   *\n";
    cout << "*******************************************\n";
    resetColor(); 
} 

void printWelcome() {
    setColor(LIGHT_GREEN); 
    cout << "Hello, Welcome to CSOPESY Command Line Interface!\n"; 
    resetColor(); 

    setColor(LIGHT_YELLOW); 
    cout << "Type 'exit' to quit, 'clear' to clear the terminal\n"; 
    resetColor();
}

void clearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 

    COORD topLeft = {0, 0}; 
    DWORD written; 
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written); 

    SetConsoleCursorPosition(hConsole, topLeft); 
    printHeader(); 
    printWelcome();
}

void processCommand(const string& cmd) {
    vector<string> validCommands = {
        "initialize", "screen", "scheduler-test", "scheduler-stop", "report-util", "clear", "exit"
    }; 

    if (find(validCommands.begin(), validCommands.end(), cmd) != validCommands.end()) {
        if (cmd == "clear") {
            clearScreen(); 
            return; 
        } 
        if (cmd == "exit") {
            exit(0);
        } 
        cout << "'" << cmd << "' command recognized. Doing something.\n"; 
    } else {
        cout << "Unknown command: " << cmd << "\n"; 
    }
} 

int main() {
    clearScreen(); 

    string input; 
    
    while (true) {
        cout << "Enter a command: "; 
        getline(cin, input); 

        // Convert to lowercase 
        transform(input.begin(), input.end(), input.begin(), ::tolower); 

        if (input.empty()) continue; 
        processCommand(input);
    }
} 
