#ifndef SCREEN_MANAGER_H 
#define SCREEN_MANAGER_H 

#include <memory> 
#include <unordered_map> 
#include <string> 
#include "ProcessScreen.h"

class ScreenManager {
private: 
    ScreenManager() = default; 
    static std::shared_ptr<ScreenManager> instance; 

    std::unordered_map<std::string, std::shared_ptr<ProcessScreen>> screens; 
    std::shared_ptr<ProcessScreen> activeScreen = nullptr; 

public: 
    static std::shared_ptr<ScreenManager> getInstance(); 

    void createScreen(const std::string& name); 
    void attachScreen(const std::string& name); 
    void detachScreen(); 
    bool screenExists(const std::string& name) const; 
    bool screenActive() const;
};

#endif