#ifndef RR_H
#define RR_H

#include <string>
#include <vector>
class MemoryManager; // Forward declaration is enough

// --- Public Function Declarations ONLY ---
int RR();
void rr_create_processes(MemoryManager& mm);
void rr_display_processes();
void rr_write_processes();
std::vector<std::string> rr_getRunningProcessNames();

#endif // RR_H