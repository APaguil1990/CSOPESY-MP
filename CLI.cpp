/** 
 * CSOPESY Command Line Interface with 
 * dynamic ASCII header and command processing. 
*/

#include <iostream> 
#include <string> 
#include <windows.h> 
#include <vector> 
#include <algorithm> 

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

/**
 * Each vector element contains line-by-line representations
 * Indexes 0-6 correspond to C, S, O, P, E, S, Y
*/
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

/**
 * Processes user commands and return response 
 * @param cmd Input command string 
 * @return Response messages or empty string for clear 
 * @details Valid commands: initialize, screen, scheduler-test, 
 *                          scheduler-stop, report-util, clear, exit
*/
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