#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <immintrin.h>
#include <mutex>
#include <condition_variable>
#include "math_utils.h"
#include "algo_utils.h"
#include "log.h"

class ThreadPool
{
public:
    ThreadPool(size_t num_threads = math_utils::max<size_t>(1, std::thread::hardware_concurrency())) 
        : workers(num_threads) 
    {
        Log::get_instance()->add_log("[ThreadPool] Initializing %zu worker threads.\n", num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers[i].thread = std::thread(&ThreadPool::worker_loop, this, i);
        }
    }
    
    ~ThreadPool() {
        Log::get_instance()->add_log("[ThreadPool] Shutting down %zu worker threads.\n", workers.size());
        stop.store(true, std::memory_order_release);
        condition.notify_all();
    
        for (auto& worker : workers) {
            if (worker.thread.joinable()) {
                worker.thread.join();
            }
        }
    }

    [[nodiscard]] size_t thread_count() const
    {
        return workers.size();
    }
    
    template<typename Func>
    void parallel_for(size_t count, Func&& func)
    {
        if (count == 0) return;

        const size_t num_workers = workers.size();
        if (count < num_workers * 4) {
            for (size_t i = 0; i < count; i++) {
                func(i);
            }
            return;
        }

        std::lock_guard<std::mutex> lock(parallel_mutex);

        const size_t chunk_size = math_utils::max<size_t>(1, count / (num_workers * 8));
        const size_t total_chunks = (count + chunk_size - 1) / chunk_size;

        dispatch_job(count, chunk_size, total_chunks, std::forward<Func>(func));
    }
    
    template<typename Func>
    void parallel_for_chunked(size_t count, Func&& func)
    {
        if (count == 0) return;

        const size_t num_workers = workers.size();
        if (count < num_workers * 4) {
            for (size_t i = 0; i < count; i++) {
                func(i, 0); 
            }
            return;
        }

        std::lock_guard<std::mutex> lock(parallel_mutex);
        
        const size_t target_chunks = num_workers * 8; 
        const size_t chunk_size = math_utils::max<size_t>(1, count / target_chunks);
        const size_t total_chunks = (count + chunk_size - 1) / chunk_size;

        dispatch_job(count, chunk_size, total_chunks, std::forward<Func>(func));
    }
    
    template<typename Func>
    void parallel_for_blocks(size_t count, size_t block_size, Func&& func)
    {
        if (count == 0) return;

        std::lock_guard<std::mutex> lock(parallel_mutex);
        
        // Bereken hoeveel blokken we totaal hebben
        const size_t total_chunks = (count + block_size - 1) / block_size;

        dispatch_job_blocks(count, block_size, total_chunks, std::forward<Func>(func));
    }

    template <class T>
    [[nodiscard]] auto enqueue(T task) -> std::future<decltype(task())>
    {
        using RetType = decltype(task());
        auto promise = std::make_shared<std::promise<RetType>>();
        auto future = promise->get_future();

        auto wrapper = [task = algo_utils::move(task), promise]() mutable {
            try {
                if constexpr (std::is_void_v<RetType>) {
                    task();
                    promise->set_value();
                } else {
                    promise->set_value(task());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        };

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            tasks.push_back(algo_utils::move(wrapper));
            has_tasks.store(true, std::memory_order_release);
        }
        condition.notify_one();
        return future;
    }

private:
    struct ParallelJob {
        alignas(64) std::atomic<size_t> next_chunk{0};
        size_t total_chunks{0};
        size_t chunk_size{0};
        size_t count{0};
        void* func_ptr{nullptr};
        void (*execute)(void* f, size_t start, size_t end, size_t chunk_id){nullptr};
    };

    struct alignas(64) Worker {
        std::thread thread;
        alignas(64) std::atomic<ParallelJob*> current_job{nullptr};
    };
    
    template<typename Func>
    void dispatch_job_blocks(size_t count, size_t chunk_size, size_t total_chunks, Func&& func)
    {
        using RawFunc = std::remove_reference_t<Func>;

        ParallelJob job;
        job.next_chunk.store(0, std::memory_order_relaxed);
        job.total_chunks = total_chunks;
        job.chunk_size = chunk_size;
        job.count = count;
        job.func_ptr = (void*)&func;
        
        // HET GROTE VERSCHIL: Geen for-loop hier! 
        // We geven de ruwe start en end index direct aan jouw lambda.
        job.execute = [](void* f, size_t start, size_t end, size_t /*chunk_id*/) {
            auto& fn = *static_cast<RawFunc*>(f);
            fn(start, end); 
        };

        active_job.store(&job, std::memory_order_release);

        if (sleeping_workers.load(std::memory_order_relaxed) > 0) {
            condition.notify_all();
        }

        // Caller-thread helpt direct mee
        while (true) {
            size_t chunk_id = job.next_chunk.fetch_add(1, std::memory_order_relaxed);
            if (chunk_id >= job.total_chunks) break;
            size_t start = chunk_id * job.chunk_size;
            size_t end = math_utils::min(start + job.chunk_size, job.count);
            job.execute(job.func_ptr, start, end, chunk_id);
        }

        active_job.store(nullptr, std::memory_order_release);

        // Wacht tot alle workers klaar zijn
        for (size_t i = 0; i < workers.size(); ++i) {
            while (workers[i].current_job.load(std::memory_order_acquire) == &job) {
                _mm_pause();
            }
        }
    }

    template<typename Func>
    void dispatch_job(size_t count, size_t chunk_size, size_t total_chunks, Func&& func)
    {
        using RawFunc = std::remove_reference_t<Func>;

        ParallelJob job;
        job.next_chunk.store(0, std::memory_order_relaxed);
        job.total_chunks = total_chunks;
        job.chunk_size = chunk_size;
        job.count = count;
        job.func_ptr = (void*)&func;
        job.execute = [](void* f, size_t start, size_t end, size_t chunk_id) {
            auto& fn = *static_cast<RawFunc*>(f);
            for (size_t i = start; i < end; ++i) {
                if constexpr (std::is_invocable_v<RawFunc, size_t, size_t>) {
                    fn(i, chunk_id);
                } else {
                    fn(i);
                }
            }
        };

        active_job.store(&job, std::memory_order_release);

        if (sleeping_workers.load(std::memory_order_relaxed) > 0) {
            condition.notify_all();
        }

        // Caller-thread helpt direct mee chunks verwerken
        while (true) {
            size_t chunk_id = job.next_chunk.fetch_add(1, std::memory_order_relaxed);
            if (chunk_id >= job.total_chunks) break;
            size_t start = chunk_id * job.chunk_size;
            size_t end = math_utils::min(start + job.chunk_size, job.count);
            job.execute(job.func_ptr, start, end, chunk_id);
        }

        // Trek de actieve job in
        active_job.store(nullptr, std::memory_order_release);

        // Wacht tot alle workers die aan DEZE specifieke job werkten klaar zijn
        for (size_t i = 0; i < workers.size(); ++i) {
            while (workers[i].current_job.load(std::memory_order_acquire) == &job) {
                _mm_pause();
            }
        }
    }

    void worker_loop(size_t worker_id)
    {
        uint32_t spin_count = 0;

        while (!stop.load(std::memory_order_relaxed))
        {
            ParallelJob* job = active_job.load(std::memory_order_acquire);
            if (job != nullptr) {
                spin_count = 0;
                workers[worker_id].current_job.store(job, std::memory_order_release);

                if (active_job.load(std::memory_order_acquire) == job) {
                    while (true) {
                        size_t chunk_id = job->next_chunk.fetch_add(1, std::memory_order_relaxed);
                        if (chunk_id >= job->total_chunks) break;
                        size_t start = chunk_id * job->chunk_size;
                        size_t end = math_utils::min(start + job->chunk_size, job->count);
                        job->execute(job->func_ptr, start, end, chunk_id);
                    }
                }

                workers[worker_id].current_job.store(nullptr, std::memory_order_release);
                _mm_pause();
                continue;
            }

            // Controleer gewone taak-queue
            if (has_tasks.load(std::memory_order_relaxed)) {
                std::function<void()> task;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (task_head < tasks.size()) {
                        task = algo_utils::move(tasks[task_head++]);
                        if (task_head == tasks.size()) {
                            tasks.clear();
                            task_head = 0;
                            has_tasks.store(false, std::memory_order_relaxed);
                        }
                    } else {
                        has_tasks.store(false, std::memory_order_relaxed);
                    }
                }
                if (task) {
                    spin_count = 0;
                    activeThreads.fetch_add(1, std::memory_order_relaxed);
                    task();
                    activeThreads.fetch_sub(1, std::memory_order_relaxed);
                    continue;
                }
            }

            // Hybride spin-backoff voor minimale latency
            if (spin_count < 2000) {
                _mm_pause();
                spin_count++;
                continue;
            } else if (spin_count < 4000) {
                std::this_thread::yield();
                spin_count++;
                continue;
            }

            // Kernel-level sleep fallback om 100% CPU-burn bij inactiviteit te voorkomen
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop.load(std::memory_order_relaxed) ||
                active_job.load(std::memory_order_relaxed) != nullptr ||
                task_head < tasks.size()) {
                continue;
            }

            sleeping_workers.fetch_add(1, std::memory_order_relaxed);
            condition.wait(lock, [this] {
                return stop.load(std::memory_order_relaxed) ||
                       task_head < tasks.size() ||
                       active_job.load(std::memory_order_relaxed) != nullptr;
            });
            sleeping_workers.fetch_sub(1, std::memory_order_relaxed);
            spin_count = 0;
        }
    }

    std::vector<Worker> workers;
    
    std::vector<std::function<void()>> tasks;
    size_t task_head = 0;
    alignas(64) std::atomic<bool> has_tasks{false};

    alignas(64) std::mutex queue_mutex;
    std::condition_variable condition;

    alignas(64) std::atomic<bool> stop{false};
    alignas(64) std::atomic<int> activeThreads{0};
    alignas(64) std::atomic<int> sleeping_workers{0};
    
    std::mutex parallel_mutex;
    alignas(64) std::atomic<ParallelJob*> active_job{nullptr};
};
