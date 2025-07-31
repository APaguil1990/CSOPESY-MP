#ifndef vmstat
#define vmstat

#include <string> 

#pragma once

void vmstats_reset();

void vmstats_increment_active_ticks();
void vmstats_increment_idle_ticks();
void vmstats_increment_paged_in();
void vmstats_increment_paged_out();

long get_total_memory();
long get_used_memory();
long get_free_memory();

long compute_used_memory(int);
long get_active_cpu_ticks();
long get_idle_cpu_ticks();
long get_total_cpu_ticks();

long get_pages_paged_in();
long get_pages_paged_out();

// For memory count updates from RR/FCFS
void vmstats_set_used_process_count(int count);

#endif