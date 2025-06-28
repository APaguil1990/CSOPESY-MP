#include "ProcessScreen.h" 
#include <iostream> 
#include <iomanip> 
#include <ctime> 
#include <Windows.h>
#include "process.h"

ProcessScreen::ProcessScreen(const std::string& name) 
    : name(name), creationTime(std::chrono::system_clock::now()), linked_pcb(nullptr) {} 

void ProcessScreen::linkPCB(std::shared_ptr<PCB> pcb) {
    linked_pcb = pcb;
}

// Formats time for display
void formatTime(char* buffer, size_t bufferSize, const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm timeinfo = {};
    localtime_s(&timeinfo, &time);
    strftime(buffer, bufferSize, "%m/%d/%Y, %I:%M:%S %p", &timeinfo);
}

void ProcessScreen::display() const {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 
    CONSOLE_SCREEN_BUFFER_INFO csbi; 
    GetConsoleScreenBufferInfo(hConsole, &csbi); 
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1; 

    char timeBuf[80]; 
    formatTime(timeBuf, sizeof(timeBuf), creationTime);

    // Display screen header 
    std::cout << std::string(width, '=') << "\n"; 
    std::cout << "Process Screen: " << name << "\n"; 
    std::cout << "Created: " << timeBuf << "\n"; 
    std::cout << "Type 'process-smi' to see details or 'exit' to return to main menu.\n";
    std::cout << std::string(width, '=') << "\n\n";

    // Show initial SMI view
    displaySMI();
}

void ProcessScreen::displaySMI() const {
    std::lock_guard<std::mutex> lock(displayMutex);

    if (!linked_pcb) {
        std::cout << "Error: Process data is not available." << std::endl;
        return;
    }

    std::cout << "Process name: " << linked_pcb->processName << std::endl;
    std::cout << "ID: " << linked_pcb->id << std::endl;
    std::cout << "Logs:" << std::endl;

    for(const auto& log : linked_pcb->logs) {
        std::cout << log; // The log should already have a newline
    }

    std::cout << "\nCurrent instruction line: " << linked_pcb->program_counter << std::endl;
    std::cout << "Lines of code: " << linked_pcb->instructions.size() << std::endl;

    if (linked_pcb->state == ProcessState::FINISHED) {
        std::cout << "\nFinished!" << std::endl;
    }
}