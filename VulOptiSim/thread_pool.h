#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <immintrin.h>

#include "algo_utils.h"
#include "math_utils.h"

// Thread-local opslag voor instant worker lookup (0 ns runtime overhead)
inline thread_local size_t g_worker_id = static_cast<size_t>(-1);

class ThreadPool
{
public:
    ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) 
        : workers(num_threads) 
    {
        for (size_t i = 0; i < num_threads; ++i) {
            // Geef de index mee aan de worker_loop om g_worker_id in te stellen
            workers[i].thread = std::thread(&ThreadPool::worker_loop, this, i);
        }
    }
    
    ~ThreadPool() {
        stop.store(true, std::memory_order_release);
        condition.notify_all();
    
        for (auto& worker : workers) {
            if (worker.thread.joinable()) {
                worker.thread.join();
            }
        }
    }

    [[nodiscard]] size_t thread_count() const noexcept {
        return workers.size();
    }
    
    // Snelle O(1) worker ID getter zonder hashing of modulo
    [[nodiscard]] size_t get_current_worker_id() const noexcept {
        return g_worker_id;
    }
    
    // Type-erased Job structuur ZONDER virtuele functies (vtable)
    struct Job {
        void (*execute_func)(void* user_data) = nullptr;
        void* user_data = nullptr;
        alignas(64) std::atomic<size_t> current_chunk{0};
        size_t count = 0;
        size_t chunk_size = 0;
        size_t total_chunks = 0;
        alignas(64) std::atomic<size_t> unfinished_chunks{0};

        [[nodiscard]] bool is_finished() const noexcept {
            return unfinished_chunks.load(std::memory_order_relaxed) == 0;
        }

        void execute_chunk() noexcept {
            size_t chunk_id = current_chunk.fetch_add(1, std::memory_order_relaxed);
            if (chunk_id < total_chunks) {
                execute_func(user_data);
                unfinished_chunks.fetch_sub(1, std::memory_order_release);
            }
        }
    };

    template<typename Func>
    void parallel_for(size_t count, Func&& func)
    {
        if (count == 0) return;

        if (count < workers.size() * 4) {
            for (size_t i = 0; i < count; ++i) {
                func(i);
            }
            return;
        }

        const size_t num_workers = workers.size();
        const size_t chunk_size = math_utils::max<size_t>(1, count / (num_workers * 8));
        const size_t total_chunks = (count + chunk_size - 1) / chunk_size;

        // Context struct op de STACK van de caller (0 heap allocaties)
        struct Context {
            Func* func_ptr;
            size_t count;
            size_t chunk_size;
            size_t total_chunks;
        } ctx { &func, count, chunk_size, total_chunks };

        Job job;
        job.user_data = &ctx;
        job.count = count;
        job.chunk_size = chunk_size;
        job.total_chunks = total_chunks;
        job.unfinished_chunks.store(total_chunks, std::memory_order_relaxed);
        
        // C-style function pointer wrapper voorkomt vtable overhead
        job.execute_func = [](void* data) {
            auto* c = static_cast<Context*>(data);
            // Haal huidig chunk_id veilig op via de Job struct
            // We berekenen de start en end indices op basis van chunk_id
        };

        // Geoptimaliseerde parallel execution context
        execute_job_parallel(job, ctx);
    }

    template<typename Func>
    void parallel_for_chunked(size_t count, Func&& func)
    {
        if (count == 0) return;

        const size_t thread_cnt = workers.size();
        if (count < thread_cnt * 4) {
            for (size_t i = 0; i < count; ++i) {
                func(i, 0); 
            }
            return;
        }

        const size_t chunk_size = (count + thread_cnt - 1) / thread_cnt;
        const size_t total_chunks = (count + chunk_size - 1) / chunk_size;

        struct Context {
            Func* func_ptr;
            size_t count;
            size_t chunk_size;
        } ctx { &func, count, chunk_size };

        Job job;
        job.user_data = &ctx;
        job.count = count;
        job.chunk_size = chunk_size;
        job.total_chunks = total_chunks;
        job.unfinished_chunks.store(total_chunks, std::memory_order_relaxed);

        execute_job_chunked_parallel(job, ctx);
    }

private:
    struct alignas(64) Worker {
        std::thread thread;
    };

    template<typename Func, typename Context>
    void execute_job_parallel(Job& job, Context& ctx) {
        job.execute_func = [](void* data) {
            // Cast context terug
            // Noot: In-place verwerking om std::function te vermijden
        };

        // Interne dispatcher logica
        dispatch_job(&job, [&ctx](size_t i, size_t chunk_id) {
            (*ctx.func_ptr)(i);
        });
    }

    template<typename Func, typename Context>
    void execute_job_chunked_parallel(Job& job, Context& ctx) {
        dispatch_job(&job, [&ctx](size_t i, size_t chunk_id) {
            (*ctx.func_ptr)(i, chunk_id);
        });
    }

    template<typename Lambda>
    void dispatch_job(Job* job, Lambda&& loop_body) {
        struct HelperContext {
            Job* job_ptr;
            Lambda* lambda_ptr;
        } h_ctx { job, &loop_body };

        job->execute_func = [](void* data) {
            auto* hc = static_cast<HelperContext*>(data);
            Job* j = hc->job_ptr;
            
            size_t chunk_id = j->current_chunk.fetch_add(1, std::memory_order_relaxed);
            if (chunk_id < j->total_chunks) {
                size_t start = chunk_id * j->chunk_size;
                size_t end = math_utils::min(start + j->chunk_size, j->count);

                for (size_t i = start; i < end; ++i) {
                    (*hc->lambda_ptr)(i, chunk_id);
                }
            }
        };

        job->user_data = &h_ctx;

        // Publiceer de job aan alle workers
        active_job.store(job, std::memory_order_release);

        // Hoofdthread helpt DIRECT mee totdat alle chunks gereserveerd zijn
        while (job->current_chunk.load(std::memory_order_relaxed) < job->total_chunks) {
            job->execute_chunk();
        }

        // Spin-wait met _mm_pause tot alle workers hun werk fysiek afgerond hebben
        while (!job->is_finished()) {
            _mm_pause();
        }

        active_job.store(nullptr, std::memory_order_release);
    }

    void worker_loop(size_t worker_index)
    {
        // 1. Sla het worker ID op in de thread_local variabele!
        g_worker_id = worker_index;

        uint32_t idle_counter = 0;
        while (!stop.load(std::memory_order_relaxed))
        {
            Job* current = active_job.load(std::memory_order_acquire);
            if (current != nullptr) {
                idle_counter = 0;
                if (!current->is_finished()) {
                    current->execute_chunk();
                    continue;
                }
            }

            // Spin-wait fase
            if (idle_counter < 4000) {
                idle_counter++;
                _mm_pause();
                continue;
            }

            // Back-off / Sleep fase bij langdurige inactiviteit
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait_for(lock, std::chrono::microseconds(50), [this] {
                return stop.load(std::memory_order_relaxed) || active_job.load(std::memory_order_relaxed) != nullptr;
            });
            idle_counter = 0;
        }
    }

    std::vector<Worker> workers;
    alignas(64) std::mutex queue_mutex;
    std::condition_variable condition;
    alignas(64) std::atomic<bool> stop{false};
    alignas(64) std::atomic<Job*> active_job{nullptr};
};