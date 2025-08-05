#ifndef FCFS_H
#define FCFS_H

#include <string>
#include <vector>
class MemoryManager; // Forward declaration is enough

// --- Public Function Declarations ONLY ---
int FCFS();
void fcfs_create_processes(MemoryManager& mm);
void fcfs_display_processes();
void fcfs_write_processes();

#endif // FCFS_H