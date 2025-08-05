#ifndef MARQUEE_CONSOLE_H
#define MARQUEE_CONSOLE_H

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <windows.h>
#include <conio.h>
#include <algorithm> // Required for std::max

// Forward declare the class
class MarqueeConsole;

// A single function to be called from CLI.cpp to run the marquee
void runMarquee();

class MarqueeConsole {
private:
    // A nested struct to hold all the state, keeping the class clean.
    struct State {
        std::string text;
        int sleepDuration;
        int x, y;
        int dx, dy;
        COORD prevMarqueePos;
        DWORD lastUpdateTime;
        std::string inputBuffer;
        std::string outputMsg;
    };

    // --- Member Variables ---
    const std::vector<std::string> HEADER_ART;
    const int HEADER_LINES;
    const int RESERVED_LINES;
    const int START_Y;
    
    int lastWidth = 0;
    HANDLE hConsole;
    CONSOLE_CURSOR_INFO cursorInfo;

    // Threading and State Management
    std::atomic<bool> running;
    std::unique_ptr<std::thread> updateThread;
    std::unique_ptr<std::thread> inputThread;
    std::mutex stateMutex;
    State state;

    // --- Private Class Methods ---
    void printHeader();
    void clearScreenPart(bool fullScreen);
    void clearPosition(COORD pos, int length);
    void processCommand(const std::string& cmd);
    void handleResize();
    void updateMarquee();
    void renderUI();
    void inputHandler();
    void initializeConsole();

public:
    MarqueeConsole();
    ~MarqueeConsole();
    void run();
};

#endif // MARQUEE_CONSOLE_H