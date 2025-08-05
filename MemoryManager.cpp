#include "MemoryManager.h"
#include "global.h" 
#include <fstream>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <condition_variable>

// --- Structs and Constants ---
const std::string BACKING_STORE_FILE = "csopesy-backing-store.txt";

struct Frame {
    bool is_free = true;
    int owner_pid = -1;
    int page_number_in_process = -1;
};

// --- PIMPL (Pointer to Implementation) Class ---
class MemoryManager::MemoryManagerImpl {
public:
    char* main_memory_buffer;
    std::vector<Frame> frame_table;
    std::fstream backing_store_stream;
    long next_backing_store_offset = 0;
    std::mutex backing_store_mutex;

    // References to scheduler queues for the eviction algorithm
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
    }

    ~MemoryManagerImpl() {
        delete[] main_memory_buffer;
        if (backing_store_stream.is_open()) { backing_store_stream.close(); }
    }

    int find_free_frame() {
        for (int i = 0; i < frame_table.size(); ++i) {
            if (frame_table[i].is_free) { return i; }
        }
        return -1;
    }

    int evict_page_oldest_process(std::atomic<int>& pages_paged_out_ref) {
        Process* oldest_process_to_evict = nullptr;
        long long min_timestamp = -1;

        auto find_oldest_in_list = [&](auto& process_list) {
            for (const auto& proc_ptr : process_list) {
                if (!proc_ptr || proc_ptr->state == ProcessState::BLOCKED) continue;
                bool has_pages_in_memory = false;
                for (const auto& pte : proc_ptr->mem_data.page_table) {
                    if (pte.is_present) { has_pages_in_memory = true; break; }
                }
                if (has_pages_in_memory && (min_timestamp == -1 || proc_ptr->mem_data.creation_timestamp < min_timestamp)) {
                    min_timestamp = proc_ptr->mem_data.creation_timestamp;
                    oldest_process_to_evict = proc_ptr.get();
                }
            }
        };
        
        { std::lock_guard<std::mutex> lock(rr_g_process_mutex);
          find_oldest_in_list(rr_ready_queue_ref);
          find_oldest_in_list(rr_running_processes_ref); }
        { std::lock_guard<std::mutex> lock(fcfs_g_process_mutex);
          find_oldest_in_list(fcfs_ready_queue_ref);
          find_oldest_in_list(fcfs_running_processes_ref); }

        if (!oldest_process_to_evict) { return -1; }

        for (int i = 0; i < oldest_process_to_evict->mem_data.page_table.size(); ++i) {
            auto& pte = oldest_process_to_evict->mem_data.page_table[i];
            if (pte.is_present) {
                int victim_frame_index = pte.frame_index;
                if (pte.is_dirty) {
                    std::lock_guard<std::mutex> lock(backing_store_mutex);
                    char* page_data_ptr = main_memory_buffer + (victim_frame_index * MEM_PER_FRAME);
                    backing_store_stream.seekp(oldest_process_to_evict->mem_data.backing_store_offset + (i * MEM_PER_FRAME));
                    backing_store_stream.write(page_data_ptr, MEM_PER_FRAME);
                    backing_store_stream.flush();
                    pages_paged_out_ref++;
                }
                pte.is_present = false;
                pte.is_dirty = false;
                pte.frame_index = -1;
                return victim_frame_index;
            }
        }
        return -1;
    }

    // --- MODIFIED: handle_page_fault ---
    void handle_page_fault(Process& faulting_process, int page_number, std::atomic<int>& paged_in_ref, std::atomic<int>& paged_out_ref) {
        // Lock the mutex for this specific process to prevent it from running
        std::unique_lock<std::mutex> lock(faulting_process.mem_data.page_fault_mutex);
        
        // Check again to see if another thread already handled the fault
        auto& pte = faulting_process.mem_data.page_table[page_number];
        if (pte.is_present) {
            return;
        }

        int frame_idx = find_free_frame();
        if (frame_idx == -1) {
            frame_idx = evict_page_oldest_process(paged_out_ref);
        }

        if (frame_idx != -1) {
            std::lock_guard<std::mutex> backing_lock(backing_store_mutex);
            char* frame_ptr = main_memory_buffer + (frame_idx * MEM_PER_FRAME);
            backing_store_stream.seekg(faulting_process.mem_data.backing_store_offset + (page_number * MEM_PER_FRAME));
            backing_store_stream.read(frame_ptr, MEM_PER_FRAME);
            
            paged_in_ref++;
            
            // Update the frame table and PTE
            frame_table[frame_idx].is_free = false;
            frame_table[frame_idx].owner_pid = faulting_process.id;
            frame_table[frame_idx].page_number_in_process = page_number;
            
            pte.is_present = true;
            pte.frame_index = frame_idx;
            pte.is_dirty = false;
            
            // Notify the blocked process that its page is ready
            faulting_process.mem_data.page_fault_cv.notify_one();
        }
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

int MemoryManager::get_used_memory_bytes() {
    int used_frames = 0;
    for (const auto& frame : p_impl->frame_table) {
        if (!frame.is_free) {
            used_frames++;
        }
    }
    return used_frames * MEM_PER_FRAME;
}

int MemoryManager::get_free_memory_bytes() {
    return MAX_OVERALL_MEM - get_used_memory_bytes();
}

void MemoryManager::allocate_for_process(Process& process, size_t requested_size) {
    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << "\nDEBUG: [MemoryManager] Allocating " << requested_size << " bytes for process '" << process.processName << "'." << std::endl;
    }
    process.mem_data.memory_size_bytes = requested_size;
    process.mem_data.creation_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int num_pages = (requested_size + MEM_PER_FRAME - 1) / MEM_PER_FRAME;
    process.mem_data.page_table.resize(num_pages);
    {
        std::lock_guard<std::mutex> lock(p_impl->backing_store_mutex);
        process.mem_data.backing_store_offset = p_impl->next_backing_store_offset;
        long new_file_end_position = p_impl->next_backing_store_offset + requested_size - 1;
        if (new_file_end_position >= p_impl->next_backing_store_offset) {
            p_impl->backing_store_stream.seekp(new_file_end_position);
            p_impl->backing_store_stream.write("\0", 1); 
        }
        p_impl->backing_store_stream.flush();
        p_impl->next_backing_store_offset += requested_size;
    }
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

// --- MODIFIED: access_memory ---
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
        process.state = ProcessState::BLOCKED;
        
        // This is a blocking call. The thread will wait here until the page is loaded.
        p_impl->handle_page_fault(process, page_number, this->pages_paged_in, this->pages_paged_out);

        // Wait for the page to be loaded.
        std::unique_lock<std::mutex> lock(process.mem_data.page_fault_mutex);
        while (!pte.is_present) {
             process.mem_data.page_fault_cv.wait(lock);
        }

        // The page is now present. The instruction needs to be re-attempted.
        // Return nullptr to signal the scheduler to put the process back in the ready queue.
        return nullptr;
    }

    if (is_write) { pte.is_dirty = true; }
    int frame_idx = pte.frame_index;
    return p_impl->main_memory_buffer + (frame_idx * MEM_PER_FRAME) + offset;
}

std::vector<MemoryManager::FrameInfo> MemoryManager::get_frame_snapshot() {
    std::vector<MemoryManager::FrameInfo> snapshot;
    snapshot.reserve(p_impl->frame_table.size());
    for (const auto& frame : p_impl->frame_table) {
        snapshot.push_back({frame.is_free, frame.owner_pid});
    }
    return snapshot;
}