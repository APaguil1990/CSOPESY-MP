#pragma once 

#include <iostream> 
#include <string> 
#include <vector> 
#include <thread> 
#include <mutex> 
#include <atomic> 
#include <memory> 
#include <windows.h> 
#include <conio.h> 

class MarqueeConsole {
private: 
    // Thread-safe state 
    struct State {
        std::string text; 
        int sleepDuration; 
        int x, y; 
        int dx, dy; 
        COORD prevMarqueePos; 
        DWORD lastUpdateTime; 
        std::string inputBuffer; 
        std::string outputMsg; 
        int pollingInterval;
    };

    const std::vector<std::string> HEADER_ART = { 
        R"(.___  ___.      ___      .______        ______      __    __   _______  _______      ______   ______   .__   __.      _______.  ______    __       _______ )",
        R"(|   \/   |     /   \     |   _  \      /  __  \    |  |  |  | |   ____||   ____|    /      | /  __  \  |  \ |  |     /       | /  __  \  |  |     |   ____|)", 
        R"(|  \  /  |    /  ^  \    |  |_)  |    |  |  |  |   |  |  |  | |  |__   |  |__      |  ,----'|  |  |  | |   \|  |    |   (----`|  |  |  | |  |     |  |__   )", 
        R"(|  |\/|  |   /  /_\  \   |      /     |  |  |  |   |  |  |  | |   __|  |   __|     |  |     |  |  |  | |  . `  |     \   \    |  |  |  | |  |     |   __|  )", 
        R"(|  |  |  |  /  _____  \  |  |\  \----.|  `--'  '--.|  `--'  | |  |____ |  |____    |  `----.|  `--'  | |  |\   | .----)   |   |  `--'  | |  `----.|  |____ )", 
        R"(|__|  |__| /__/     \__\ | _| `._____| \_____\_____\\______/  |_______||_______|    \______| \______/  |__| \__| |_______/     \______/  |_______||_______|)"
    };

    // Constants 
    const int HEADER_LINES = HEADER_ART.size(); 
    const int RESERVED_LINES = 3; 
    const int START_Y = HEADER_LINES + 1; 
    int lastWidth = 0; 

    // Console handles 
    HANDLE hConsole; 
    CONSOLE_CURSOR_INFO cursorInfo; 

    // Thread manager 
    std::atomic<bool> running; 
    std::unique_ptr<std::thread> updateThread; 
    std::unique_ptr<std::thread> inputThread; 
    std::mutex stateMutex; 
    State state;
    
    // Core functionality 
    int getMonitorRefreshRate(); 
    void printHeader(); 
    void clearScreen(bool withHeader = false); 
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