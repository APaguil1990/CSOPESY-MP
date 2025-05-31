#include "ScreenManager.h" 
#include "ProcessScreen.h"
#include <iostream> 
#include <Windows.h>

// Initialize static instance pointer 
std::shared_ptr<ScreenManager> ScreenManager::instance = nullptr; 

// Get singleton instance 
std::shared_ptr<ScreenManager> ScreenManager::getInstance() {
    if (!instance) {
        instance.reset(new ScreenManager());
    } 
    return instance; 
} 

// Create new screen 
void ScreenManager::createScreen(const std::string& name) {
    if (!screenExists(name)) {
        screens[name] = std::make_shared<ProcessScreen>(name); 
        std::cout << "Create Screen: " << name << std::endl; 
    } 
    // attachScreen(name);
}

// Switch to existing screen 
void ScreenManager::attachScreen(const std::string& name) {
    if (screenExists(name)) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 
        COORD topLeft = {0, 0}; 
        CONSOLE_SCREEN_BUFFER_INFO csbi; 
        DWORD written; 
        GetConsoleScreenBufferInfo(hConsole, &csbi); 
        FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written); 
        SetConsoleCursorPosition(hConsole, topLeft);

        activeScreen = screens[name]; 
        activeScreen->display(); 
        // activeScreen->run(); // Start screen's thread
    } else {
        std::cerr << "Error : Screen '" << name << "' not found!" << std::endl;  
    }
}

// Detatch current screen 
void ScreenManager::detachScreen() {
    if (activeScreen) {
        // activeScreen-> stop(); 
        activeScreen = nullptr;
    }
}

// Check if screen exists 
bool ScreenManager::screenExists(const std::string& name) const {
    return screens.find(name) != screens.end(); 
}

// Check if screen is active 
bool ScreenManager::screenActive() const {
    return activeScreen != nullptr;
}