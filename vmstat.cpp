#include "vmstat.h"
#include "config.h" // for MEM_PER_PROC, MAX_OVERALL_MEM

#include <atomic>

static std::atomic<long> used_memory(0);
static std::atomic<long> active_ticks(0);
static std::atomic<long> idle_ticks(0);
static std::atomic<long> paged_in(0);
static std::atomic<long> paged_out(0);


void vmstats_reset() {
    active_ticks = 0;
    idle_ticks = 0;
    paged_in = 0;
    paged_out = 0;
}

void vmstats_increment_active_ticks() { active_ticks++; }
void vmstats_increment_idle_ticks()   { idle_ticks++; }
void vmstats_increment_paged_in()     { paged_in++; }
void vmstats_increment_paged_out()    { paged_out++; }

long get_total_memory() {
    return MAX_OVERALL_MEM;
}

long compute_used_memory(int memory_process){
    used_memory = memory_process * MIN_MEM_PER_PROC;
    return 0;
}

long get_free_memory() {
    return get_total_memory() - get_used_memory();
}

long get_used_memory()      { return used_memory; }
long get_active_cpu_ticks() { return active_ticks; }
long get_idle_cpu_ticks()   { return idle_ticks; }
long get_total_cpu_ticks()  { return active_ticks + idle_ticks; }
long get_pages_paged_in()   { return paged_in; }
long get_pages_paged_out()  { return paged_out; }
