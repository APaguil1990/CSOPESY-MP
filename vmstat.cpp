#include "vmstat.h"
#include "config.h" 
#include "global.h" // Include this to get access to the memory_manager

// --- State for CPU Ticks (This is still correct) ---
static std::atomic<long> active_ticks(0);
static std::atomic<long> idle_ticks(0);

// --- OBSOLETE static variables for memory are REMOVED ---
// static std::atomic<long> used_memory(0);
// static std::atomic<long> paged_in(0);
// static std::atomic<long> paged_out(0);


void vmstats_reset() {
    active_ticks = 0;
    idle_ticks = 0;
    // No need to reset memory stats here anymore.
}

// These functions are still correct.
void vmstats_increment_active_ticks() { active_ticks++; }
void vmstats_increment_idle_ticks()   { idle_ticks++; }

// --- OBSOLETE functions are REMOVED ---
// void vmstats_increment_paged_in()
// void vmstats_increment_paged_out()
// long compute_used_memory(int memory_process)

// --- MODIFIED functions that now report from the MemoryManager ---

long get_total_memory() {
    return MAX_OVERALL_MEM;
}

long get_used_memory() {
    // Safely check if the manager exists before using it.
    if (memory_manager) {
        return memory_manager->get_used_memory_bytes();
    }
    return 0;
}

long get_free_memory() {
    if (memory_manager) {
        return memory_manager->get_free_memory_bytes();
    }
    // Before initialization, free memory is all memory.
    return MAX_OVERALL_MEM;
}

long get_pages_paged_in() {
    if (memory_manager) {
        // Read the value from the atomic counter in the MemoryManager.
        return memory_manager->pages_paged_in.load();
    }
    return 0;
}

long get_pages_paged_out() {
    if (memory_manager) {
        // Read the value from the atomic counter in the MemoryManager.
        return memory_manager->pages_paged_out.load();
    }
    return 0;
}

// --- Unchanged CPU tick functions ---
long get_active_cpu_ticks() { return active_ticks; }
long get_idle_cpu_ticks()   { return idle_ticks; }
long get_total_cpu_ticks()  { return active_ticks + idle_ticks; }