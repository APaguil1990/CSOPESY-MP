#define NOMINMAX // Crucial to prevent windows.h from breaking std::max
#include "MarqueeConsole.h"
#include <sstream>
#include <chrono>
#include <iomanip>

using namespace std;
using namespace std::chrono_literals;

// This is the function you call from CLI.cpp
void runMarquee() {
    try {
        MarqueeConsole console;
        console.run();
    } catch (const exception& e) {
        cerr << "Error in MarqueeConsole: " << e.what() << endl;
    }
}

// Constructor
MarqueeConsole::MarqueeConsole() :
    HEADER_ART({
        R"(.___  ___.      ___      .______        ______      __    __   _______  _______      ______   ______   .__   __.      _______.  ______    __       _______ )",
        R"(|   \/   |     /   \     |   _  \      /  __  \    |  |  |  | |   ____||   ____|    /      | /  __  \  |  \ |  |     /       | /  __  \  |  |     |   ____|)",
        R"(|  \  /  |    /  ^  \    |  |_)  |    |  |  |  |   |  |  |  | |  |__   |  |__      |  ,----'|  |  |  | |   \|  |    |   (----`|  |  |  | |  |     |  |__   )",
        R"(|  |\/|  |   /  /_\  \   |      /     |  |  |  |   |  |  |  | |   __|  |   __|     |  |     |  |  |  | |  . `  |     \   \    |  |  |  | |  |     |   __|  )",
        R"(|  |  |  |  /  _____  \  |  |\  \----.|  `--'  '--.|  `--'  | |  |____ |  |____    |  `----.|  `--'  | |  |\   | .----)   |   |  `--'  | |  `----.|  |____ )",
        R"(|__|  |__| /__/     \__\ | _| `._____| \_____\_____\\______/  |_______||_______|    \______| \______/  |__| \__| |_______/     \______/  |_______||_______|)"
    }),
    HEADER_LINES(HEADER_ART.size()),
    RESERVED_LINES(3),
    START_Y(HEADER_LINES + 1),
    hConsole(GetStdHandle(STD_OUTPUT_HANDLE)),
    running(true) {

    state = {
        "CSOPESY Marquee! Commands: speed <ms>, text <new_text>, clear, quit",
        50, 0, START_Y, 1, 1,
        {0, static_cast<SHORT>(START_Y)}, 0, "", "Enter a command below."
    };

    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = false;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
    initializeConsole();
}

// Destructor
MarqueeConsole::~MarqueeConsole() {
    running = false;
    if (updateThread && updateThread->joinable()) updateThread->join();
    if (inputThread && inputThread->joinable()) inputThread->join();
    cursorInfo.bVisible = true;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}

void MarqueeConsole::run() {
    updateThread = make_unique<thread>(&MarqueeConsole::updateMarquee, this);
    inputThread = make_unique<thread>(&MarqueeConsole::inputHandler, this);
    renderUI();
}

void MarqueeConsole::updateMarquee() {
    while (running) {
        lock_guard<mutex> lock(stateMutex);
        DWORD currentTime = GetTickCount();
        if (currentTime - state.lastUpdateTime >= static_cast<DWORD>(state.sleepDuration)) {
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hConsole, &csbi);
            const int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            const int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            const int maxY = height - RESERVED_LINES - 1;

            clearPosition(state.prevMarqueePos, state.text.length());
            state.x += state.dx;
            state.y += state.dy;
            
            const int textLen = static_cast<int>(state.text.length());
            const int xBound = std::max(0, width - textLen);

            if (state.x < 0) { state.x = 0; state.dx *= -1; }
            else if (state.x > xBound) { state.x = xBound; state.dx *= -1; }

            if (state.y < START_Y) { state.y = START_Y; state.dy *= -1; }
            else if (state.y > maxY) { state.y = maxY; state.dy *= -1; }

            state.prevMarqueePos = {static_cast<SHORT>(state.x), static_cast<SHORT>(state.y)};
            SetConsoleCursorPosition(hConsole, state.prevMarqueePos);
            cout << state.text << flush;
            state.lastUpdateTime = currentTime;
        }
        this_thread::sleep_for(1ms);
    }
}

void MarqueeConsole::inputHandler() {
    while (running) {
        if (_kbhit()) {
            char ch = _getch();
            string commandToProcess;
            { 
                lock_guard<mutex> lock(stateMutex);
                if (ch == '\r') {
                    commandToProcess = state.inputBuffer;
                    state.inputBuffer.clear();
                } else if (ch == '\b' && !state.inputBuffer.empty()) {
                    state.inputBuffer.pop_back();
                } else if (isprint(ch)) {
                    state.inputBuffer += ch;
                }
            }
            if (!commandToProcess.empty()) {
                processCommand(commandToProcess);
                if (commandToProcess == "quit") {
                    running = false;
                }
            }
        }
        this_thread::sleep_for(10ms);
    }
}

void MarqueeConsole::renderUI() {
    while (running) {
        handleResize();
        {
            lock_guard<mutex> lock(stateMutex);
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hConsole, &csbi);
            int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            int maxY = height - RESERVED_LINES - 1;

            COORD inputPos = {0, static_cast<SHORT>(maxY + 1)};
            SetConsoleCursorPosition(hConsole, inputPos);
            cout << "Input Command: " << left << setw(width - 16) << state.inputBuffer;

            COORD outputPos = {0, static_cast<SHORT>(maxY + 2)};
            SetConsoleCursorPosition(hConsole, outputPos);
            cout << left << setw(width) << state.outputMsg.substr(0, width);
        }
        this_thread::sleep_for(33ms);
    }
}

void MarqueeConsole::processCommand(const string& cmd) {
    lock_guard<mutex> lock(stateMutex);
    istringstream iss(cmd);
    string action;
    iss >> action;
    transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "speed") {
        int newSpeed;
        if (iss >> newSpeed && newSpeed > 0) {
            state.sleepDuration = newSpeed;
            state.outputMsg = "Speed changed to " + to_string(newSpeed) + " ms.";
        } else {
            state.outputMsg = "Invalid speed value. Must be a positive number.";
        }
    } else if (action == "text") {
        string newText;
        getline(iss, newText);
        size_t first = newText.find_first_not_of(' ');
        if (string::npos != first) newText = newText.substr(first);
        
        if (!newText.empty()) {
            clearPosition(state.prevMarqueePos, state.text.length());
            state.text = newText;
            state.outputMsg = "Text changed.";
        } else {
            state.outputMsg = "Error: Please provide text after 'text' command.";
        }
    } else if (action == "clear") {
        clearScreenPart(true);
        state.outputMsg = "Terminal cleared.";
    } else if (action == "quit") {
        state.outputMsg = "Quitting...";
    } else {
        state.outputMsg = "Unknown command: '" + cmd + "'";
    }
}

void MarqueeConsole::initializeConsole() {
    clearScreenPart(true);
}

void MarqueeConsole::printHeader() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    lastWidth = width;

    COORD headerPos = {0, 0};
    SetConsoleCursorPosition(hConsole, headerPos);

    for (const string& line : HEADER_ART) {
        int padding = (width - static_cast<int>(line.length())) / 2;
        padding = std::max(0, padding);
        cout << string(padding, ' ') << line << endl;
    }
}

void MarqueeConsole::handleResize() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    if (width != lastWidth) {
        lock_guard<mutex> lock(stateMutex);
        clearScreenPart(true);
    }
}

void MarqueeConsole::clearPosition(COORD pos, int length) {
    DWORD written;
    FillConsoleOutputCharacterA(hConsole, ' ', length, pos, &written);
}

void MarqueeConsole::clearScreenPart(bool fullScreen) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    DWORD written;
    COORD startPos = {0, 0};
    DWORD cellsToClear = csbi.dwSize.X * csbi.dwSize.Y;

    if (!fullScreen) {
        startPos.Y = HEADER_LINES;
        cellsToClear = csbi.dwSize.X * (csbi.dwSize.Y - HEADER_LINES);
    }

    FillConsoleOutputCharacter(hConsole, ' ', cellsToClear, startPos, &written);
    SetConsoleCursorPosition(hConsole, startPos);

    if (fullScreen) {
        printHeader();
    }
}