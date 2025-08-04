#include "MemoryManager.h"
#include "config.h" // For global config variables
#include <fstream>
#include <iostream>
#include <algorithm>
#include <mutex>

// Defined in RR.cpp and FCFS.cpp, needed for locking
extern std::mutex rr_g_process_mutex;
extern std::mutex fcfs_g_process_mutex;

const std::string BACKING_STORE_FILE = "csopesy-backing-store.txt";

struct Frame {
    bool is_free = true;
    int owner_pid = -1;
    int page_number_in_process = -1;
};

// --- PIMPL Implementation ---
class MemoryManager::MemoryManagerImpl {
public:
    char* main_memory_buffer;
    std::vector<Frame> frame_table;
    std::fstream backing_store_stream;
    long next_backing_store_offset = 0;

    // References to the process lists from the schedulers
    std::deque<std::shared_ptr<Process>>& rr_ready_queue_ref;
    std::vector<std::shared_ptr<Process>>& rr_running_processes_ref;
    std::deque<std::shared_ptr<Process>>& fcfs_ready_queue_ref;
    std::vector<std::shared_ptr<Process>>& fcfs_running_processes_ref;

    MemoryManagerImpl(
        std::deque<std::shared_ptr<Process>>& rr_ready,
        std::vector<std::shared_ptr<Process>>& rr_running,
        std::deque<std::shared_ptr<Process>>& fcfs_ready,
        std::vector<std::shared_ptr<Process>>& fcfs_running)
        : rr_ready_queue_ref(rr_ready), rr_running_processes_ref(rr_running),
          fcfs_ready_queue_ref(fcfs_ready), fcfs_running_processes_ref(fcfs_running) {
        
        main_memory_buffer = new char[MAX_OVERALL_MEM]();
        int num_frames = MAX_OVERALL_MEM / MEM_PER_FRAME;
        frame_table.resize(num_frames);

        backing_store_stream.open(BACKING_STORE_FILE, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!backing_store_stream) {
            std::cerr << "FATAL: Could not open backing store: " << BACKING_STORE_FILE << std::endl;
            exit(1);
        }
    }

    ~MemoryManagerImpl() {
        delete[] main_memory_buffer;
        if (backing_store_stream.is_open()) backing_store_stream.close();
    }

    int find_free_frame() {
        for (int i = 0; i < frame_table.size(); ++i) {
            if (frame_table[i].is_free) return i;
        }
        return -1;
    }

    int evict_page_oldest_process(int& pages_paged_out_ref) {
        Process* oldest_process = nullptr;
        long long min_timestamp = -1;

        auto find_oldest_in_list = [&](auto& process_list) {
            for (const auto& proc_ptr : process_list) {
                if (!proc_ptr) continue;
                bool has_pages_in_memory = false;
                for (const auto& pte : proc_ptr->mem_data.page_table) {
                    if (pte.is_present) {
                        has_pages_in_memory = true;
                        break;
                    }
                }
                if (has_pages_in_memory && (min_timestamp == -1 || proc_ptr->mem_data.creation_timestamp < min_timestamp)) {
                    min_timestamp = proc_ptr->mem_data.creation_timestamp;
                    oldest_process = proc_ptr.get();
                }
            }
        };
        
        // This is tricky because lists are protected by different mutexes
        { std::lock_guard<std::mutex> lock(rr_g_process_mutex);
          find_oldest_in_list(rr_ready_queue_ref);
          find_oldest_in_list(rr_running_processes_ref); }
        { std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
          find_oldest_in_list(fcfs_ready_queue_ref);
          find_oldest_in_list(fcfs_running_processes_ref); }

        if (!oldest_process) {
            std::cerr << "FATAL: No evictable page found!" << std::endl; exit(1);
        }

        for (int i = 0; i < oldest_process->mem_data.page_table.size(); ++i) {
            auto& pte = oldest_process->mem_data.page_table[i];
            if (pte.is_present) {
                int victim_frame_index = pte.frame_index;
                if (pte.is_dirty) {
                    char* page_data_ptr = main_memory_buffer + (victim_frame_index * MEM_PER_FRAME);
                    backing_store_stream.seekp(oldest_process->mem_data.backing_store_offset + (i * MEM_PER_FRAME));
                    backing_store_stream.write(page_data_ptr, MEM_PER_FRAME);
                    pages_paged_out_ref++;
                }
                pte.is_present = false;
                pte.is_dirty = false;
                pte.frame_index = -1;
                return victim_frame_index;
            }
        }
        return -1; // Should not happen
    }

    void handle_page_fault(Process& faulting_process, int page_number, int& paged_in_ref, int& paged_out_ref) {
        int frame_idx = find_free_frame();
        if (frame_idx == -1) {
            frame_idx = evict_page_oldest_process(paged_out_ref);
        }
        
        char* frame_ptr = main_memory_buffer + (frame_idx * MEM_PER_FRAME);
        backing_store_stream.seekg(faulting_process.mem_data.backing_store_offset + (page_number * MEM_PER_FRAME));
        backing_store_stream.read(frame_ptr, MEM_PER_FRAME);
        paged_in_ref++;
        
        frame_table[frame_idx].is_free = false;
        frame_table[frame_idx].owner_pid = faulting_process.id;
        frame_table[frame_idx].page_number_in_process = page_number;
        
        auto& pte = faulting_process.mem_data.page_table[page_number];
        pte.is_present = true;
        pte.frame_index = frame_idx;
        pte.is_dirty = false;
    }
};

// --- Public Method Implementations ---

MemoryManager::MemoryManager(
    std::deque<std::shared_ptr<Process>>& rr_ready_queue,
    std::vector<std::shared_ptr<Process>>& rr_running_processes,
    std::deque<std::shared_ptr<Process>>& fcfs_ready_queue,
    std::vector<std::shared_ptr<Process>>& fcfs_running_processes) {
    p_impl = new MemoryManagerImpl(rr_ready_queue, rr_running_processes, fcfs_ready_queue, fcfs_running_processes);
    pages_paged_in = 0;
    pages_paged_out = 0;
}

MemoryManager::~MemoryManager() {
    delete p_impl;
}

int MemoryManager::get_free_memory_bytes() {
    int free_frames = 0;
    for (const auto& frame : p_impl->frame_table) {
        if (frame.is_free) free_frames++;
    }
    return free_frames * MEM_PER_FRAME;
}

int MemoryManager::get_used_memory_bytes() {
    return MAX_OVERALL_MEM - get_free_memory_bytes();
}

void MemoryManager::allocate_for_process(Process& process, size_t requested_size) {
     std::cout << "\nDEBUG: [MemoryManager] Allocating " << requested_size << " bytes for process '" << process.processName << "'." << std::endl;
    process.mem_data.memory_size_bytes = requested_size;
    process.mem_data.backing_store_offset = p_impl->next_backing_store_offset;
    process.mem_data.creation_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    int num_pages = (requested_size + MEM_PER_FRAME - 1) / MEM_PER_FRAME;
    process.mem_data.page_table.resize(num_pages);
    
    // NOTE: For thread safety, the file operations should be locked.
    // If you add a std::mutex p_impl->backing_store_mutex;, you would lock it here.
    // std::lock_guard<std::mutex> lock(p_impl->backing_store_mutex);
    
    std::vector<char> zero_buffer(requested_size, 0);
    p_impl->backing_store_stream.seekp(p_impl->next_backing_store_offset);
    p_impl->backing_store_stream.write(zero_buffer.data(), requested_size);

    // Force the operating system to write the buffered data to the actual disk file.
    p_impl->backing_store_stream.flush();
    
    p_impl->next_backing_store_offset += requested_size;
}

void MemoryManager::deallocate_for_process(Process& process) {
    for (int i = 0; i < p_impl->frame_table.size(); ++i) {
        if (p_impl->frame_table[i].owner_pid == process.id) {
            p_impl->frame_table[i].is_free = true;
            p_impl->frame_table[i].owner_pid = -1;
            p_impl->frame_table[i].page_number_in_process = -1;
        }
    }
}

char* MemoryManager::access_memory(Process& process, int logical_address, bool is_write) {
    if (logical_address < 0 || logical_address >= process.mem_data.memory_size_bytes) {
        process.mem_data.terminated_by_error = true;
        process.mem_data.termination_reason = "Memory access violation at address " + std::to_string(logical_address);
        return nullptr;
    }

    int page_number = logical_address / MEM_PER_FRAME;
    int offset = logical_address % MEM_PER_FRAME;

    auto& pte = process.mem_data.page_table[page_number];
    if (!pte.is_present) {
        p_impl->handle_page_fault(process, page_number, this->pages_paged_in, this->pages_paged_out);
        process.state = ProcessState::BLOCKED; // Mark process as blocked
        return nullptr; // Signal PAGE FAULT
    }

    if (is_write) pte.is_dirty = true;
    int frame_idx = pte.frame_index;
    return p_impl->main_memory_buffer + (frame_idx * MEM_PER_FRAME) + offset;
    
}

std::vector<MemoryManager::FrameInfo> MemoryManager::get_frame_snapshot() {
    std::vector<MemoryManager::FrameInfo> snapshot;
    // You should lock a mutex here if you were concerned about thread safety
    // during the snapshot, but for this project it is likely fine.
    snapshot.reserve(p_impl->frame_table.size());
    for (const auto& frame : p_impl->frame_table) {
        snapshot.push_back({frame.is_free, frame.owner_pid});
    }
    return snapshot;
}