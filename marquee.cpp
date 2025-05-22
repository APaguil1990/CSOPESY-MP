#include <iostream>
#include <string>
#include <windows.h>
#include <conio.h>    
#include <sstream>    
#include <algorithm>  
#include <iterator>

using namespace std;

// --- Globals ---
const int RESERVED_LINES = 3;
string text = "Hello World, I am a Marquee Text";
int sleepDuration = 50;
int x = 0, y = 0;           
int dx = 1, dy = 1;     
DWORD lastUpdateTime = 0;     // Track animation timing 
COORD prevMarqueePos = {5, 5};

// --- Function to clear text at position ---
void clearPosition(COORD pos, int length) {
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
    cout << string(length, ' ');
}

// --- Process Commands ---
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
        string newText = cmd.substr(cmd.find(' ') + 1);
        clearPosition(prevMarqueePos, text.length());
        text = newText;
        outputMsg = "Text changed to '" + newText + "'";
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

// --- Main ---
int main() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    string inputBuffer, outputMsg;
    
    // Hide cursor
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = false;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    while (true) {
        DWORD currentTime = GetTickCount();
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        int maxX = csbi.srWindow.Right;
        int maxY = csbi.srWindow.Bottom - RESERVED_LINES;

        // ---Always Clear Previous Marquee Position --- 
        clearPosition(prevMarqueePos, text.length()); 

        // --- Update Marquee Only When Needed ---
        if (currentTime - lastUpdateTime >= sleepDuration) {
            // Clear old position
            clearPosition({static_cast<SHORT>(x), static_cast<SHORT>(y)}, text.length());

            // Update position
            x += dx;
            y += dy;

            // Bounce logic with clamping
            if (x < 0 || x > maxX - text.length()) dx *= -1; 
            if (y < 0 || y > maxY) dy *= -1; 

            // Clamp to bounds 
            x = max(0, min(x, maxX - (int)text.length())); 
            y = max(0, min(y, maxY)); 

            prevMarqueePos = {static_cast<SHORT>(x), static_cast<SHORT>(y)}; 
            lastUpdateTime = currentTime;
        }

        // -- Draw New Maruqee Position 
        SetConsoleCursorPosition(hConsole, prevMarqueePos); 
        cout << text; 

        // --- Process Input & UI Immediately ---
        if (_kbhit()) {
            char ch = _getch();
            if (ch == '\r') { 
                if (inputBuffer == "quit" || inputBuffer == "Quit") break;
                processCommand(inputBuffer, outputMsg);
                inputBuffer.clear();
            } else if (ch == '\b' && !inputBuffer.empty()) {
                inputBuffer.pop_back();
            } else if (isprint(ch)) {
                inputBuffer += ch;
            }
        }

        // --- Update UI ---
        COORD inputPos = {0, static_cast<SHORT>(maxY + 1)};
        SetConsoleCursorPosition(hConsole, inputPos);
        cout << "Input Command: " << inputBuffer << string(maxX - 15 - inputBuffer.length(), ' '); 

        COORD outputPos = {0, static_cast<SHORT>(maxY + 2)};
        SetConsoleCursorPosition(hConsole, outputPos);
        cout << outputMsg << string(maxX - outputMsg.length(), ' '); 

        outputMsg = outputMsg.substr(0, maxX); 

        // -- Clear Command -- 
        if (outputMsg.find("cleared") != string::npos) {
            outputMsg.clear();
        } 

        Sleep(10);
    }

    // Clean exit
    cursorInfo.bVisible = true;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
    cout << "\nGoodbye, Have a Nice Day :)\n";
    return 0;
}