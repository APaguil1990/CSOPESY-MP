#include "MarqueeConsole.h" 
#include <sstream> 
#include <algorithm> 
#include <chrono> 

using namespace std; 
using namespace std::chrono_literals; 

// Constructor: Initializes console state and hides cursor
MarqueeConsole::MarqueeConsole() : 
    hConsole(GetStdHandle(STD_OUTPUT_HANDLE)), 
    running(true) {
    
    // Initialize marquee text and position
    state = {
        "Hello World, I am a Marquee Text Implemented with Multithreading :)", 
        50, 
        0,
        START_Y, 
        1, 
        1, 
        {0, static_cast<SHORT>(START_Y)}, 
        0, 
        "", 
        "", 
        10
    };

    // Set cursor invisible
    GetConsoleCursorInfo(hConsole, &cursorInfo); 
    cursorInfo.bVisible = false; 
    SetConsoleCursorInfo(hConsole, &cursorInfo); 
    
    // Clear console and print header
    initializeConsole();

    // Cache current console width
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    lastWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

// Destructor: Stops threads and restores cursor
MarqueeConsole::~MarqueeConsole() {
    running = false; 

    // Wait threads to finish 
    if (updateThread && updateThread->joinable()) updateThread->join(); 
    if (inputThread && inputThread->joinable()) inputThread->join(); 

    // Restore cursor 
    cursorInfo.bVisible = true; 
    SetConsoleCursorInfo(hConsole, &cursorInfo); 
} 

int MarqueeConsole::getMonitorRefreshRate() {
    DEVMODE devMode = {}; 
    devMode.dmSize = sizeof(DEVMODE); 

    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devMode)) {
        return devMode.dmDisplayFrequency; 
    }
    return 60;
}

// Display ASCII header at top of screen 
void MarqueeConsole::printHeader() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1; 
    lastWidth = width; 

    COORD headerPos = {0, 0}; 
    SetConsoleCursorPosition(hConsole, headerPos); 

    for (const string& line : HEADER_ART) {
        string displayLine = line.substr(0, static_cast<size_t>(width));
        int padding = (width - static_cast<int>(line.length())) / 2; 
        padding = max(0, padding); 
        cout << string(padding, ' ') << displayLine << string(width - padding - displayLine.length(), ' ') << endl;
    }
}

// Clear the console
void MarqueeConsole::clearScreen(bool withHeader) {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 

    COORD clearStart = {0, static_cast<SHORT>(withHeader ? 0 : HEADER_LINES)}; 
    DWORD cells = csbi.dwSize.X * (csbi.dwSize.Y - (withHeader ? 0 : HEADER_LINES)); 
    DWORD written; 

    FillConsoleOutputCharacter(hConsole, ' ', cells, clearStart, &written); 
    SetConsoleCursorPosition(hConsole, clearStart); 

    if (withHeader) printHeader();
}

// Clear single text line at specific console coordinate
void MarqueeConsole::clearPosition(COORD pos, int length) {
    if (pos.Y >= HEADER_LINES) {
        SetConsoleCursorPosition(hConsole, pos); 
        cout << string(length, ' ') << flush; 
    }
}

// Parses and handles user input commands like speed, text, clear, and quit
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
            state.outputMsg = format("Changed speed to {}", newSpeed); 
        } else {
            state.outputMsg = "Invalid speed value!"; 
        }
    } else if (action == "text") {
        size_t pos = cmd.find(' '); 

        if (pos != string::npos && pos + 1 < cmd.length()) {
            string newText = cmd.substr(pos + 1); 
            clearPosition(state.prevMarqueePos, state.text.length()); 
            state.text = newText; 
            state.outputMsg = format("Text changed to '{}'", newText); 
        } else {
            state.outputMsg = "Error: Please provide text after 'text' command :)"; 
        }
    } else if (action == "pollrate") {
        int newRate; 

        if (iss >> newRate && newRate >= 1 && newRate <= 1000) {
            state.pollingInterval = newRate; 
            state.outputMsg = format("Polling interval set to {} ms", newRate);
        } else {
            state.outputMsg = "Invalid pollrate value (1 - 1000 ms allowed)";
        }
    } else if (action == "clear") {
        // Only clear bottom input/output lines, not full screen
        stateMutex.unlock(); 
        CONSOLE_SCREEN_BUFFER_INFO csbi; 
        GetConsoleScreenBufferInfo(hConsole, &csbi); 
        int maxX = csbi.srWindow.Right; 
        int maxY = csbi.srWindow.Bottom; 

        COORD inputPos = {0, static_cast<SHORT>(maxY - 1)}; 
        COORD outputPos = {0, static_cast<SHORT>(maxY)}; 

        DWORD written; 
        FillConsoleOutputCharacter(hConsole, ' ', maxX, inputPos, &written); 
        FillConsoleOutputCharacter(hConsole, ' ', maxX, outputPos, &written); 
        SetConsoleCursorPosition(hConsole, inputPos); 
        stateMutex.lock();

        state.outputMsg = "Terminal cleared.";
    } else if (action == "quit" || action == "Quit") {
        running = false; 
        state.outputMsg = "Goodbye, Have a Nice Day :)"; 
    } else {
        state.outputMsg = format("Unknown command: {}", cmd);
    }
} 

// Handles terminal resize events and reprints header if needed
void MarqueeConsole::handleResize() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1; 

    if (width != lastWidth) {
        lock_guard<mutex> lock(stateMutex); 
        clearScreen(true); 
        lastWidth = width;
    }
}

// Updates marquee text's movement and bouncing behavior
void MarqueeConsole::updateMarquee() {
    int refreshRate = getMonitorRefreshRate(); 
    int frameDelay = 1000 / refreshRate;
    
    while (running) {
        handleResize();
        DWORD currentTime = GetTickCount(); 

        if (state.y < START_Y) {
            state.y = START_Y; 
            state.dy *= -1;
        }

        {
            lock_guard<mutex> lock(stateMutex); 

            if (currentTime - state.lastUpdateTime >= state.sleepDuration) {
                CONSOLE_SCREEN_BUFFER_INFO csbi; 
                GetConsoleScreenBufferInfo(hConsole, &csbi); 
                const int maxX = csbi.srWindow.Right; 
                const int maxY = csbi.srWindow.Bottom - RESERVED_LINES; 

                clearPosition(state.prevMarqueePos, state.text.length()); 

                state.x += state.dx; 
                state.y += state.dy; 

                const int textLen = static_cast<int>(state.text.length()); 
                const int xBound = max(0, maxX - textLen); 
                const int yBound = max(START_Y + 1, maxY); 

                if (state.x < 0 || state.x > xBound) state.dx *= -1; 
                if (state.y < START_Y || state.y > yBound) state.dy *= -1; 

                state.x = clamp(state.x, 0, xBound); 
                state.y = clamp(state.y, START_Y, yBound); 

                state.prevMarqueePos= {
                    static_cast<SHORT>(state.x), 
                    static_cast<SHORT>(state.y) 
                };
                state.lastUpdateTime = currentTime; 

                SetConsoleCursorPosition(hConsole, state.prevMarqueePos); 
                cout << state.text << flush; 
            }
        }
        // this_thread::sleep_for(10ms); 
        this_thread::sleep_for(std::chrono::milliseconds(frameDelay));
    }
}

// Draws input and output UI components at the bottom 
void MarqueeConsole::renderUI() {
    while (running) {
        handleResize(); 

        if (stateMutex.try_lock()) {
            CONSOLE_SCREEN_BUFFER_INFO csbi; 
            GetConsoleScreenBufferInfo(hConsole, &csbi); 
            int maxX = csbi.srWindow.Right; 
            int maxY = csbi.srWindow.Bottom - RESERVED_LINES; 

            COORD inputPos = {0, static_cast<SHORT>(maxY + 1)}; 
            SetConsoleCursorPosition(hConsole, inputPos); 
            cout << format("Input Command: {:<{}}", state.inputBuffer, maxX - 15); 

            COORD outputPos = {0, static_cast<SHORT>(maxY + 2)}; 
            SetConsoleCursorPosition(hConsole, outputPos); 
            cout << format("{:<{}}", state.outputMsg.substr(0, maxX), maxX); 

            stateMutex.unlock();
        }
        this_thread::sleep_for(20ms);
    }
}

// Collects keystrokes in real time and builds input commands
void MarqueeConsole::inputHandler() {
    int refreshRate = getMonitorRefreshRate(); 
    int frameDelay = 1000 / refreshRate; 
    
    while (running) {
        if (_kbhit()) {
            char ch = _getch(); 
            std::string command;

            {
                lock_guard<mutex> lock(stateMutex); 
                if (ch == '\r') {
                    command = state.inputBuffer; 
                    state.inputBuffer.clear(); 
                } else if (ch == '\b' && !state.inputBuffer.empty()) {
                    state.inputBuffer.pop_back(); 
                } else if (isprint(ch)) {
                    state.inputBuffer += ch; 
                }
            }

            // Process command *after* releasing lock
            if (!command.empty()) {
                processCommand(command); 
            }
        }
        // this_thread::sleep_for(10ms); 
        this_thread::sleep_for(std::chrono::milliseconds(frameDelay));
        this_thread::sleep_for(chrono::milliseconds(state.pollingInterval));
    }
}

// Clears screen and pritns initial header at top 
void MarqueeConsole::initializeConsole() {
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 

    COORD topLeft = {0, 0}; 
    DWORD written; 
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written); 
    SetConsoleCursorPosition(hConsole, topLeft); 
    printHeader(); 
}

// Main thread: Starts and reders the UI
void MarqueeConsole::run() {
    // Start threads 
    updateThread = make_unique<thread>( [this] { updateMarquee(); } ); 
    inputThread = make_unique<thread>( [this] { this->inputHandler(); } ); 

    // Main UI 
    renderUI(); 

    {
        lock_guard<mutex> lock(stateMutex); 

        CONSOLE_SCREEN_BUFFER_INFO csbi; 
        GetConsoleScreenBufferInfo(hConsole, &csbi); 
        int maxX = csbi.srWindow.Right; 
        int maxY = csbi.srWindow.Bottom - RESERVED_LINES; 

        // Output message at bottom 
        COORD outputPos = {0, static_cast<SHORT>(maxY + 2)}; 
        SetConsoleCursorPosition(hConsole, outputPos); 
        std::cout << std::format("{:<{}}", state.outputMsg.substr(0, maxX), maxX);
    }

    // Assurance prompt doesn't overwrite message 
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    COORD exitPos = {0, static_cast<SHORT>(csbi.srWindow.Bottom)}; 
    SetConsoleCursorPosition(hConsole, exitPos); 
    std::cout << std::endl << std::flush; 
}

int main() {
    try {
        MarqueeConsole console; 
        console.run(); 
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl; 
        return EXIT_FAILURE; 
    } 
    return EXIT_SUCCESS;
}