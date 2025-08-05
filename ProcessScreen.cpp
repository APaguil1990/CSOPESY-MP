#include "ProcessScreen.h" 
#include <iostream> 
#include <iomanip> 
#include <ctime> 
#include <chrono> 
#include <Windows.h>
#include <mutex> 

ProcessScreen::ProcessScreen(const std::string& name) 
    : name(name), currentLine(0), totalLines(1000), 
    creationTime(std::chrono::system_clock::now()) {} 

// ProcessScreen::~ProcessScreen() {
//     stop();
// } 

void ProcessScreen::display() const {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1; 

    // Readable format for time 
    auto time = std::chrono::system_clock::to_time_t(creationTime); 
    std::tm timeinfo = {}; 
    std::tm* tmp = std::localtime(&time); 
    if (tmp) timeinfo = *tmp;
    
    char timeBuf[80]; 
    strftime(timeBuf, sizeof(timeBuf), "%m/%d/%Y, %I:%M:%S %p", &timeinfo); 

    // Display screen header 
    std::cout << std::string(width, '=') << "\n"; 
    std::cout << "Process Screen: " << name << "\n"; 
    std::cout << "Created: " << timeBuf << "\n"; 
    std::cout << std::string(width, '=') << "\n\n";

    // Static progress display
    // std::cout << "Progress: 0/1000 (0%)\n" << std::endl;
}

// void ProcessScreen::run() {
//     if (running) return; 

//     running = true; 
//     progressThread.reset(new std::thread(&ProcessScreen::updateProgress, this));
// }

// void ProcessScreen::stop() {
//     if (running) {
//         running = false; 

//         if (progressThread && progressThread->joinable()) {
//             progressThread->join();
//         }
//      }
// }

// void ProcessScreen::updateProgress() {
//     HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 
//     CONSOLE_SCREEN_BUFFER_INFO csbi; 
//     GetConsoleScreenBufferInfo(hConsole, &csbi); 

//     const SHORT progressLine = csbi.srWindow.Bottom - 2; 
    
//     while (running) {
//         // Simulate progress
//         if (currentLine < totalLines) {
//             currentLine++; 
//         } 

//         {
//             std::lock_guard<std::mutex> lock(displayMutex); 
//             HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 
//             CONSOLE_SCREEN_BUFFER_INFO csbi; 
//             GetConsoleScreenBufferInfo(hConsole, &csbi); 

//             COORD pos = {0, 5};
//             SetConsoleCursorPosition(hConsole, pos);

//             int percentatge = (currentLine*100) / totalLines; 
//             std::cout << "Progress: " << currentLine << "/" << totalLines << " (" << percentatge << "%)      ";
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     }
// }