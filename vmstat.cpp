#include "vmstat.h"
#include "global.h" // Include this to get access to the global memory_manager

// --- State for CPU Ticks (This is still managed locally by vmstat) ---
static std::atomic<long> active_ticks(0);
static std::atomic<long> idle_ticks(0);

void vmstats_reset() {
    active_ticks = 0;
    idle_ticks = 0;
    // No need to reset memory stats here; MemoryManager handles its own state.
}

void vmstats_increment_active_ticks() { active_ticks++; }
void vmstats_increment_idle_ticks()   { idle_ticks++; }

// --- MODIFIED functions that now report directly from the MemoryManager or globals ---

long get_total_memory() {
    // This value is from the config and stored globally.
    return MAX_OVERALL_MEM;
}

long get_used_memory() {
    // Safely check if the manager exists before using it.
    if (memory_manager) {
        return memory_manager->get_used_memory_bytes();
    }
    return 0; // Return 0 if the system isn't initialized yet.
}

long get_free_memory() {
    if (memory_manager) {
        return memory_manager->get_free_memory_bytes();
    }
    // Before initialization, free memory is all of the memory.
    return MAX_OVERALL_MEM;
}

long get_pages_paged_in() {
    if (memory_manager) {
        // Read the value directly from the atomic counter in the MemoryManager.
        return memory_manager->pages_paged_in.load();
    }
    return 0;
}

long get_pages_paged_out() {
    if (memory_manager) {
        // Read the value directly from the atomic counter in the MemoryManager.
        return memory_manager->pages_paged_out.load();
    }
    return 0;
}

// --- Unchanged CPU tick functions ---
long get_active_cpu_ticks() { return active_ticks.load(); }
long get_idle_cpu_ticks()   { return idle_ticks.load(); }
long get_total_cpu_ticks()  { return active_ticks.load() + idle_ticks.load(); }