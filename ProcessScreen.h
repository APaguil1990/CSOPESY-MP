#ifndef PROCESS_SCREEN_H 
#define PROCESS_SCREEN_H 

#include "Process.h" // Use the new unified PCB
#include <string> 
#include <chrono> 
#include <memory> 
#include <mutex>

class ProcessScreen {
private: 
    std::string name; 
    std::chrono::system_clock::time_point creationTime; 
    
    // Link to the actual process data (PCB)
    std::shared_ptr<PCB> linked_pcb;
    mutable std::mutex displayMutex;

public: 
    explicit ProcessScreen(const std::string& name); 

    // Link the screen to a PCB
    void linkPCB(std::shared_ptr<PCB> pcb);

    // Displays the main screen view
    void display() const; 

    // Displays the process-smi information
    void displaySMI() const;
};

#endif