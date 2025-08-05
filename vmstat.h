#ifndef VMSTAT_H
#define VMSTAT_H

#pragma once

// Keep functions for CPU ticks, as they are still managed here.
void vmstats_reset();
void vmstats_increment_active_ticks();
void vmstats_increment_idle_ticks();

// These functions will now get their data from the MemoryManager.
long get_total_memory();
long get_used_memory();
long get_free_memory();
long get_active_cpu_ticks();
long get_idle_cpu_ticks();
long get_total_cpu_ticks();
long get_pages_paged_in();
long get_pages_paged_out();

#endif // VMSTAT_H