#ifndef PROCESS_SCREEN_H 
#define PROCESS_SCREEN_H 

#include <string> 
#include <chrono> 
// #include <memory> 
// #include <atomic> 
// #include <thread> 
// #include <mutex> 

class ProcessScreen {
private: 
    void updateProgress(); 

    std::string name; 
    int currentLine;
    // std::atomic<int> currentLine; 
    int totalLines; 
    std::chrono::system_clock::time_point creationTime; 

    // std::atomic<bool> running; 
    // std::unique_ptr<std::thread> progressThread; 
    // mutable std::mutex displayMutex;

public: 
    explicit ProcessScreen(const std::string& name); 
    // ~ProcessScreen(); 

    void display() const; 
    // void run(); 
    // void stop(); 
};

#endif