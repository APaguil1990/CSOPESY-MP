#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <conio.h>
#include <sstream>
#include <algorithm>
#include <numeric>

using namespace std;

// --- Constants and Global Handle ---
const vector<string> HEADER_ART = {
    R"(.___  ___.      ___      .______        ______      __    __   _______  _______      ______   ______   .__   __.      _______.  ______    __       _______ )",
    R"(|   \/   |     /   \     |   _  \      /  __  \    |  |  |  | |   ____||   ____|    /      | /  __  \  |  \ |  |     /       | /  __  \  |  |     |   ____|)",
    R"(|  \  /  |    /  ^  \    |  |_)  |    |  |  |  |   |  |  |  | |  |__   |  |__      |  ,----'|  |  |  | |   \|  |    |   (----`|  |  |  | |  |     |  |__   )",
    R"(|  |\/|  |   /  /_\  \   |      /     |  |  |  |   |  |  |  | |   __|  |   __|     |  |     |  |  |  | |  . `  |     \   \    |  |  |  | |  |     |   __|  )",
    R"(|  |  |  |  /  _____  \  |  |\  \----.|  `--'  '--.|  `--'  | |  |____ |  |____    |  `----.|  `--'  | |  |\   | .----)   |   |  `--'  | |  `----.|  |____ )",
    R"(|__|  |__| /__/     \__\ | _| `._____| \_____\_____\\______/  |_______||_______|    \______| \______/  |__| \__| |_______/     \______/  |_______||_______|)"
};
const int HEADER_LINES = HEADER_ART.size();
const int RESERVED_LINES = 3; // For UI (prompt, status, and a buffer line)
const int START_Y = HEADER_LINES; // Start marquee right below the header
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

// --- Forward Declarations ---
void clearPosition(COORD pos, int length);
void clearScreen();

// --- State Structure (Encapsulates all dynamic data) ---
struct MarqueeState {
    string text = "Hello World! Resize the window or try commands: 'speed 50', 'text <your_message>', 'quit'";
    DWORD animationDelay = 50;
    int x = 0, y = START_Y;
    int dx = 1, dy = 1;
    DWORD lastUpdateTime = 0;
    COORD prevMarqueePos = {0, static_cast<SHORT>(START_Y)};
    string inputBuffer;
    string outputMsg = "Welcome! Type a command and press Enter.";
    bool running = true;
};

// --- Functions ---
void printHeader() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    COORD headerPos = {0, 0};
    SetConsoleCursorPosition(hConsole, headerPos);

    for (const string& line : HEADER_ART) {
        int padding = (width - static_cast<int>(line.length())) / 2;
        padding = max(0, padding);
        cout << string(padding, ' ') << line << endl;
    }
}

void clearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);

    COORD clearStart = {0, static_cast<SHORT>(HEADER_LINES)};
    DWORD cells = csbi.dwSize.X * (csbi.dwSize.Y - HEADER_LINES);
    DWORD written;

    FillConsoleOutputCharacter(hConsole, ' ', cells, clearStart, &written);
    SetConsoleCursorPosition(hConsole, clearStart);
}

void clearPosition(COORD pos, int length) {
    // Only clear if it's within the drawable area
    if (pos.Y >= HEADER_LINES) {
        SetConsoleCursorPosition(hConsole, pos);
        cout << string(length, ' ') << flush;
    }
}

void processCommand(MarqueeState& state) {
    if (state.inputBuffer.empty()) {
        return;
    }

    istringstream iss(state.inputBuffer);
    string action;
    iss >> action;
    transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "speed") {
        int newSpeed;
        if (iss >> newSpeed && newSpeed > 0) {
            state.animationDelay = newSpeed;
            state.outputMsg = "Animation delay set to " + to_string(newSpeed) + "ms.";
        } else {
            state.outputMsg = "Invalid speed value! Must be a positive number.";
        }
    } else if (action == "text") {
        string newText;
        if (iss >> ws && getline(iss, newText) && !newText.empty()) {
            clearPosition(state.prevMarqueePos, state.text.length());
            state.text = newText;
            state.outputMsg = "Text changed to '" + newText + "'.";
        } else {
            state.outputMsg = "Error: Please provide text after 'text' command.";
        }
    } else if (action == "clear") {
        clearScreen();
        state.outputMsg = "Marquee area cleared.";
    } else if (action == "quit") {
        state.outputMsg = "Goodbye, have a nice day!";
        state.running = false;
    } else {
        state.outputMsg = "Unknown command: '" + action + "'.";
    }
}

void handleInput(MarqueeState& state) {
    if (_kbhit()) {
        char ch = _getch();
        if (ch == '\r') { // Enter key
            processCommand(state);
            state.inputBuffer.clear();
        } else if (ch == '\b' && !state.inputBuffer.empty()) { // Backspace
            state.inputBuffer.pop_back();
        } else if (isprint(static_cast<unsigned char>(ch))) {
            state.inputBuffer += ch;
        }
    }
}

void updateMarquee(MarqueeState& state) {
    DWORD currentTime = GetTickCount();
    if (currentTime - state.lastUpdateTime < state.animationDelay) {
        return;
    }
    state.lastUpdateTime = currentTime;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    const int termWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    const int termHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    const int marqueeHeight = termHeight - HEADER_LINES - RESERVED_LINES;
    if (marqueeHeight < 1) {
        clearPosition(state.prevMarqueePos, state.text.length());
        return;
    }

    clearPosition(state.prevMarqueePos, state.text.length());

    state.x += state.dx;
    state.y += state.dy;

    const int textLen = static_cast<int>(state.text.length());
    const int xBound = max(0, termWidth - textLen);
    const int yBound = START_Y + marqueeHeight - 1;

    if (state.x <= 0) { state.x = 0; state.dx = 1; }
    else if (state.x >= xBound) { state.x = xBound; state.dx = -1; }

    if (state.y <= START_Y) { state.y = START_Y; state.dy = 1; }
    else if (state.y >= yBound) { state.y = yBound; state.dy = -1; }

    state.prevMarqueePos = {static_cast<SHORT>(state.x), static_cast<SHORT>(state.y)};
    SetConsoleCursorPosition(hConsole, state.prevMarqueePos);
    cout << state.text << flush;
}

void renderUI(const MarqueeState& state) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int ui_y_pos = csbi.srWindow.Bottom - (RESERVED_LINES - 2);

    COORD inputPos = {0, static_cast<SHORT>(ui_y_pos)};
    SetConsoleCursorPosition(hConsole, inputPos);
    string prompt = "Input > ";
    string line = prompt + state.inputBuffer;
    cout << line << string(max(0, width - (int)line.length()), ' ');

    COORD outputPos = {0, static_cast<SHORT>(ui_y_pos + 1)};
    SetConsoleCursorPosition(hConsole, outputPos);
    string msg = "Status: " + state.outputMsg;
    cout << msg.substr(0, width) << string(max(0, width - (int)msg.length()), ' ');

    SetConsoleCursorPosition(hConsole, {static_cast<SHORT>(line.length()), static_cast<SHORT>(ui_y_pos)});
    cout << flush;
}

int main() {
    // Maximize the console window on startup
    HWND hwnd = GetConsoleWindow();
    ShowWindow(hwnd, SW_MAXIMIZE);

    // Hide blinking cursor for the duration of the program
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = false;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Initial clear and setup using a system call for simplicity
    system("cls");
    printHeader();

    MarqueeState appState;

    while (appState.running) {
        handleInput(appState);
        updateMarquee(appState);
        renderUI(appState);
        Sleep(10);
    }

    // --- Graceful Cleanup ---
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    COORD exitPos = {0, static_cast<SHORT>(csbi.srWindow.Bottom)};
    SetConsoleCursorPosition(hConsole, exitPos);

    cout << "Status: " << appState.outputMsg << endl;

    cursorInfo.bVisible = true;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    return 0;
}