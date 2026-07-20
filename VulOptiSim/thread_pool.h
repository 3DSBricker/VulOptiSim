#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <iostream>
#include <algorithm>
#include <immintrin.h>

class ThreadPool
{
public:
    explicit ThreadPool(size_t numThreads)
    {
        tasks.reserve(1024); 
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool()
    {
        stop.store(true, std::memory_order_release);
        condition.notify_all();
        for (auto& thread : workers) {
            if (thread.joinable()) thread.join();
        }
    }

    [[nodiscard]] size_t thread_count() const
    {
        return workers.size();
    }
    
    
    size_t get_current_worker_id() const
    {
        return std::hash<std::thread::id>{}(std::this_thread::get_id()) %  workers.size();
    }
    
    template<typename Func>
    void parallel_for(size_t count, Func&& func)
    {
        if (count == 0) return;

        if (count < workers.size() * 4) {
            for (size_t i = 0; i < count; i++) {
                func(i);
            }
            return;
        }

        const size_t chunk_size = std::max<size_t>(1, count / (workers.size() * 8));
        const size_t total_chunks = (count + chunk_size - 1) / chunk_size;

        // Mutex en CV zijn verwijderd uit de Context
        struct Context {
            std::atomic<size_t> current_chunk{0};
            size_t count;
            size_t chunk_size;
            size_t total_chunks;
            Func* f_ptr;

            Context(size_t c, size_t cs, size_t tc, Func* func_ptr) 
                : count(c), chunk_size(cs), total_chunks(tc), f_ptr(func_ptr) {}
        };

        Context ctx(count, chunk_size, total_chunks, &func);
        const size_t tasks_to_queue = std::min(workers.size(), total_chunks);
        
        std::atomic<size_t> threads_running{ tasks_to_queue + 1 };

        auto worker_task = [&ctx, &threads_running]() {
            while (true) {
                size_t chunk_id = ctx.current_chunk.fetch_add(1, std::memory_order_relaxed);
                if (chunk_id >= ctx.total_chunks) {
                    break; 
                }

                size_t start = chunk_id * ctx.chunk_size;
                size_t end = std::min(start + ctx.chunk_size, ctx.count);

                for (size_t i = start; i < end; ++i) {
                    (*ctx.f_ptr)(i); 
                }
            }
            
            // Simpelweg afmelden zonder lock, dankzij memory_order_release
            threads_running.fetch_sub(1, std::memory_order_release);
        };

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            for(size_t i = 0; i < tasks_to_queue; ++i)
            {
                tasks.push_back(worker_task);
            }
        }
        condition.notify_all();

        worker_task(); // De hoofdthread helpt direct mee

        // ADAPTIVE SPIN-WAIT (Dé oplossing voor je freeze)
        // _mm_pause() is hardware-level wachten: 0% OS overhead, 0 context-swaps.
        while (threads_running.load(std::memory_order_acquire) > 0) {
            _mm_pause(); 
        } 
    }
    
    template<typename Func>
    void parallel_for_chunked(size_t count, Func&& func)
    {
        if (count == 0) return;

        const size_t thread_count = workers.size();
        if (count < thread_count * 4) {
            for (size_t i = 0; i < count; i++) {
                func(i, 0); 
            }
            return;
        }

        const size_t chunk_size = (count + thread_count - 1) / thread_count;
        const size_t total_chunks = (count + chunk_size - 1) / chunk_size;

        struct Context {
            std::atomic<size_t> current_chunk{0};
            size_t count;
            size_t chunk_size;
            size_t total_chunks;
            Func* f_ptr;

            Context(size_t c, size_t cs, size_t tc, Func* func_ptr) 
                : count(c), chunk_size(cs), total_chunks(tc), f_ptr(func_ptr) {}
        };
        
        Context ctx(count, chunk_size, total_chunks, &func);
        const size_t tasks_to_queue = std::min(workers.size(), total_chunks);
        
        std::atomic<size_t> threads_running{ tasks_to_queue + 1 };

        auto worker_task = [&ctx, &threads_running]() {
            while (true) {
                size_t chunk_id = ctx.current_chunk.fetch_add(1, std::memory_order_relaxed);
                if (chunk_id >= ctx.total_chunks) {
                    break; 
                }

                size_t start = chunk_id * ctx.chunk_size;
                size_t end = std::min(start + ctx.chunk_size, ctx.count);

                for (size_t i = start; i < end; ++i) {
                    (*ctx.f_ptr)(i, chunk_id);
                }
            }
            
            // Simpelweg afmelden zonder lock
            threads_running.fetch_sub(1, std::memory_order_release);
        };

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            for(size_t i = 0; i < tasks_to_queue; ++i)
            {
                tasks.push_back(worker_task);
            }
        }
        condition.notify_all();

        worker_task(); // Main thread helpt mee

        // ADAPTIVE SPIN-WAIT
        // _mm_pause() is hardware-level wachten: 0% OS overhead, 0 context-swaps.
        while (threads_running.load(std::memory_order_acquire) > 0) {
            _mm_pause(); 
        }
    }

    template <class T>
    [[nodiscard]] auto enqueue(T task) -> std::future<decltype(task())>
    {
        using RetType = decltype(task());
        auto promise = std::make_shared<std::promise<RetType>>();
        auto future = promise->get_future();

        auto wrapper = [task = std::move(task), promise]() mutable {
            if constexpr (std::is_void_v<RetType>) {
                task();
                promise->set_value();
            } else {
                promise->set_value(task());
            }
        };

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            tasks.push_back(std::move(wrapper));
        }
        condition.notify_one();
        return future;
    }

    void printActiveThreads() const
    {
        std::cout << "Active threads: " << activeThreads.load(std::memory_order_relaxed) << std::endl;
    }

private:
    
    void worker_loop()
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                condition.wait(lock, [this] { 
                    return stop.load(std::memory_order_acquire) || task_head < tasks.size(); 
                });

                if (stop.load(std::memory_order_acquire) && task_head == tasks.size()) {
                    return; 
                }

                task = std::move(tasks[task_head++]);
                
                if (task_head == tasks.size()) {
                    tasks.clear();
                    task_head = 0;
                }
            } 

            activeThreads.fetch_add(1, std::memory_order_relaxed);
            task(); 
            activeThreads.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    std::vector<std::thread> workers;
    std::vector<std::function<void()>> tasks;
    size_t task_head = 0;

    alignas(64) std::mutex queue_mutex;
    std::condition_variable condition;

    alignas(64) std::atomic<bool> stop{false};
    alignas(64) std::atomic<int> activeThreads{0};
};