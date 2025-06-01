#include <iostream> 
#include <string> 
#include <windows.h> 
#include <conio.h> 
#include <sstream> 
#include <algorithm> 
#include <iterator> 

using namespace std; 

// Global variables 
const int RESERVED_LINES = 3; 
const int START_Y = 2; 
string text = "Hello World, I am a Marquee Text"; 
int sleepDuration = 50; 
int x = 0, y = START_Y; 
int dx = 1, dy = 1; 
DWORD lastUpdateTime = 0; 
COORD prevMarqueePos = {0, static_cast<SHORT>(START_Y)}; 
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 

// Clear screen 
void clearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 

    COORD topLeft = {0, 0}; 
    DWORD written; 
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written); 
    SetConsoleCursorPosition(hConsole, topLeft); 
} 

// Clear position 
void clearPosition(COORD pos, int length) {
    SetConsoleCursorPosition(hConsole, pos); 
    cout << string(length, ' ') << flush; 
} 

// Process commands 
void processCommand(const string& cmd, string& outputMsg) {
    istringstream iss(cmd);
    string action;
    iss >> action;

    transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "speed") {
        int newSpeed;

        if (iss >> newSpeed && newSpeed > 0) {
            sleepDuration = newSpeed;
            outputMsg = "Changed speed to " + to_string(newSpeed);
        } else {
            outputMsg = "Invalid speed value!";
        }
    } else if (action == "text") {
        size_t pos = cmd.find(' '); 

        if (pos != string::npos && pos + 1 < cmd.length()) {
            string newText = cmd.substr(pos + 1);
            clearPosition(prevMarqueePos, text.length());
            text = newText;
            outputMsg = "Text changed to '" + newText + "'";
        } else {
            outputMsg = "Error: Please provide text after 'text' command :)";
        }
    } else if (action == "clear") {
        // Clear entire marquee area + UI 
        _CONSOLE_SCREEN_BUFFER_INFO csbi; 
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi); 
        int maxX = csbi.srWindow.Right; 
        int maxY = csbi.srWindow.Bottom; 
        
        // Clear all lines 
        for (short i = 0; i < maxY; i++) {
            COORD linePos = {0, i}; 
            clearPosition(linePos, maxX); 
        }

        outputMsg = "Terminal cleared.";
    } else if (action == "quit") {
        outputMsg = "Goodbye, Have a Nice Day :)";
    } else {
        outputMsg = "Unknown command: " + cmd;
    }
}

// Handle input 
void handleInput(string& inputBuffer, string& outputMsg, bool& running) {
    if (_kbhit()) {
        char ch = _getch(); 

        if (ch == '\r') {
            if (inputBuffer == "quit" || inputBuffer == "Quit") running = false; 
            processCommand(inputBuffer, outputMsg); 
            inputBuffer.clear(); 
        } else if (ch == '\b' && !inputBuffer.empty()) {
            inputBuffer.pop_back(); 
        } else if (isprint(ch)) {
            inputBuffer += ch;
        }
    }
}

// Update marquee 
void updateMarquee() {
    DWORD currentTime = GetTickCount(); 

    if (currentTime - lastUpdateTime >= sleepDuration) {
        CONSOLE_SCREEN_BUFFER_INFO csbi; 
        GetConsoleScreenBufferInfo(hConsole, &csbi); 
        const int maxX = csbi.srWindow.Right; 
        const int maxY = csbi.srWindow.Bottom - RESERVED_LINES; 

        clearPosition(prevMarqueePos, text.length());

        x += dx; 
        y += dy; 

        // Calculate the safe boundaries
        const int textLen = static_cast<int>(text.length()); 
        const int xBound = std::max(0, maxX - textLen); 
        const int yBound = maxY;

        // Bounce logic 
        if (x < 0 || x > maxX - text.length()) dx *= -1; 
        if (y < START_Y || y > maxY) dy *= -1; 

        // x = max(0, min(x, maxX - (int)text.length())); 
        // y = max(START_Y, min(y, maxY)); 
        
        x = std::clamp(x, 0, xBound); 
        y = std::clamp(y, START_Y, yBound);

        prevMarqueePos = {static_cast<SHORT>(x), static_cast<SHORT>(y)}; 
        lastUpdateTime = currentTime; 

        SetConsoleCursorPosition(hConsole, prevMarqueePos); 
        cout << text << flush; 
    }
}

// Render UI 
void renderUI(const string& inputBuffer, const string& outputMsg) {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    int maxX = csbi.srWindow.Right; 
    int maxY = csbi.srWindow.Bottom - RESERVED_LINES; 

    COORD inputPos = {0, static_cast<SHORT>(maxY + 1)}; 
    SetConsoleCursorPosition(hConsole, inputPos); 
    cout << "Input Command: " << inputBuffer << string(maxX - 15 - inputBuffer.length(), ' '); 

    COORD outputPos = {0, static_cast<SHORT>(maxY + 2)}; 
    SetConsoleCursorPosition(hConsole, outputPos); 
    cout << outputMsg.substr(0, maxX) << string(maxX - outputMsg.length(), ' ') << flush;
}

// Main 
int main() {
    CONSOLE_CURSOR_INFO cursorInfo; 
    GetConsoleCursorInfo(hConsole, &cursorInfo); 
    cursorInfo.bVisible = false; 
    SetConsoleCursorInfo(hConsole, &cursorInfo); 

    // Clear terminal on startup
    clearScreen(); 

    string inputBuffer, outputMsg; 
    bool running = true; 

    while (running) {
        updateMarquee(); 
        handleInput(inputBuffer, outputMsg, running); 
        renderUI(inputBuffer, outputMsg); 
        Sleep(10); 
    }

    // Clean exit below marquee area 
    cursorInfo.bVisible = true; 
    SetConsoleCursorInfo(hConsole, &cursorInfo); 

    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    COORD exitPos = {0, static_cast<SHORT>(csbi.srWindow.Bottom + 1)}; 
    SetConsoleCursorPosition(hConsole, exitPos); 

    return 0;
}