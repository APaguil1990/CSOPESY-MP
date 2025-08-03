#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <deque>
#include <vector>
#include <memory>
#include "Process.h" // Use our new unified Process class

class MemoryManager {
private:
    class MemoryManagerImpl;
    MemoryManagerImpl* p_impl;

public:
    // --- STATISTICS FOR VMSTAT ---
    int pages_paged_in;
    int pages_paged_out;
    int get_free_memory_bytes();
    int get_used_memory_bytes();

    // --- CONSTRUCTOR & DESTRUCTOR ---
    MemoryManager(
        // Give the manager access to all lists where processes can be
        std::deque<std::shared_ptr<Process>>& rr_ready_queue,
        std::vector<std::shared_ptr<Process>>& rr_running_processes,
        std::deque<std::shared_ptr<Process>>& fcfs_ready_queue,
        std::vector<std::shared_ptr<Process>>& fcfs_running_processes
    );
    ~MemoryManager();

    // --- LIFECYCLE MANAGEMENT ---
    void allocate_for_process(Process& process, size_t requested_size);
    void deallocate_for_process(Process& process);

    // --- CORE FUNCTIONALITY ---
    // The CPU/Scheduler calls this for every READ or WRITE instruction.
    char* access_memory(Process& process, int logical_address, bool is_write);
    
    struct FrameInfo {
        bool is_free;
        int owner_pid;
    };
    std::vector<FrameInfo> get_frame_snapshot();
};

#endif // MEMORY_MANAGER_H