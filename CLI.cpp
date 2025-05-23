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

SHORT inputLineY = 0; 
SHORT outputLineY = 0; 

// Console handle 
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 

// ASCII character patterns (C,S,O,P,E,S,Y) 
const vector<vector<string>> charPatterns = {
    { //C
        " ##### ", 
        "##   ##",
        "##     ", 
        "##     ", 
        "##   ##", 
        " ##### "
    }, 
    { // S
        " ##### ", 
        "##   ##", 
        "   ### ", 
        " ###   ", 
        "##   ##", 
        " ##### "
    }, 
    {// O
        " ##### ", 
        "##   ##", 
        "##   ##", 
        "##   ##", 
        "##   ##", 
        " ##### "
    }, 
    { // P
        "###### ", 
        "##   ##", 
        "###### ", 
        "##     ", 
        "##     ", 
        "##     "     
    }, 
    { // E
        "###### ", 
        "##     ", 
        "#####  ", 
        "##     ", 
        "##     ", 
        "###### "
    }, 
    { // S
        " ##### ", 
        "##   ##", 
        "   ### ", 
        " ###   ", 
        "##   ##", 
        " ##### "
    }, 
    { // Y
        "##   ##", 
        "##   ##", 
        " ##### ", 
        "  ##   ", 
        "  ##   ", 
        "  ##   " 
    }
};

void setColor(int color) {
    SetConsoleTextAttribute(hConsole, color); 
} 

void resetColor() {
    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR); 
} 

void printCentered(const string& text, int width) {
    int padding = (width - text.length()) / 2; 
    padding = max(0, padding);
    cout << string(padding, ' ') << text << endl;
}


void printHeader() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1; 
    width = max(width, 60);

    setColor(DEFAULT_COLOR); // Cyan for header
    cout << string(width, '*') << endl; 

    // Generate CSOPESY 
    string headerText = "CSOPESY"; // Use string to avoid null terminator
    for(int row = 0; row < 6; row++) {
        string line; 
        for(char c : headerText) { // Iterate over characters without null
            int idx = headerText.find(c); 
            line += charPatterns[idx][row] + " "; 
        }
        printCentered(line, width);
    }

    cout << string(width, '*') << endl; 
    resetColor(); 
}

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

void initializePositions() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    inputLineY = csbi.dwCursorPosition.Y; 
    outputLineY = inputLineY + 1;
}

void clearOutputLine() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 

    COORD outputPos = {0, outputLineY}; 
    SetConsoleCursorPosition(hConsole, outputPos); 
    cout << string(csbi.srWindow.Right, ' ');
}

string processCommand(const string& cmd) {
    vector<string> validCommands = {
        "initialize", "screen", "scheduler-test", 
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
    return "Unknown command: " + cmd;
}

int main() {
    clearScreen();
    initializePositions();

    string input;
    while(true) {
        // Show input prompt
        COORD inputPos = {0, inputLineY};
        SetConsoleCursorPosition(hConsole, inputPos);
        cout << "Enter a command: ";
        
        // Get input
        getline(cin, input);
        
        // Clear input line
        SetConsoleCursorPosition(hConsole, inputPos);
        cout << string(80, ' ');  // Clear entire input line
        
        if(input.empty()) continue;
        transform(input.begin(), input.end(), input.begin(), ::tolower);
        
        string output = processCommand(input);
        
        // Handle clear command's empty output
        if(output.empty()) continue;

        // Clear and show output
        clearOutputLine();
        COORD outputPos = {0, outputLineY};
        SetConsoleCursorPosition(hConsole, outputPos);
        cout << output;
    }
    
    return 0;
}