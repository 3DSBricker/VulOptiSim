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
    
    ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) : workers(num_threads) {
        for (size_t i = 0; i < num_threads; ++i) {
            // Gebruik de bestaande (en correcte) worker_loop in plaats van de losse lambda
            workers[i].thread = std::thread(&ThreadPool::worker_loop, this);
        }
    }
    
    ~ThreadPool() {
        // Zet de juiste stop variabele (die worker_loop gebruikt) op true
        stop.store(true, std::memory_order_release);
        stop_all.store(true, std::memory_order_relaxed);
    
        // Maak alle threads wakker die in condition.wait() hangen, anders hangt je game bij het afsluiten
        condition.notify_all();
    
        for (auto& worker : workers) {
            if (worker.thread.joinable()) {
                worker.thread.join();
            }
        }
    }
    
    // Lock-free taak toewijzing
    void assign_task(size_t thread_idx, std::function<void()> task) {
        workers[thread_idx].task = std::move(task);
        workers[thread_idx].has_task.store(true, std::memory_order_release);
    }

    [[nodiscard]] size_t thread_count() const
    {
        return workers.size();
    }
    
    size_t get_current_worker_id() const
    {
        return std::hash<std::thread::id>{}(std::this_thread::get_id()) % workers.size();
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

        Job<Func, void> job(count, chunk_size, total_chunks, std::forward<Func>(func), false);
        
        // Publiceer de taak lock-free aan alle worker threads
        active_job.store(&job, std::memory_order_release);

        // Hoofdthread helpt direct mee met uitvoeren
        while (!job.is_finished()) {
            job.execute_chunk();
        }

        // Wacht tot alle worker threads ook klaar zijn (0 OS overhead, puur hardware _mm_pause)
        while (!job.is_finished()) {
            _mm_pause();
        }

        active_job.store(nullptr, std::memory_order_release);
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
        
        Job<Func, void> job(count, chunk_size, total_chunks, std::forward<Func>(func), true);
        
        active_job.store(&job, std::memory_order_release);

        while (!job.is_finished()) {
            job.execute_chunk();
        }

        while (!job.is_finished()) {
            _mm_pause();
        }

        active_job.store(nullptr, std::memory_order_release);
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
    
    struct JobBase {
        virtual void execute_chunk() = 0;
        virtual bool is_finished() const = 0;
        virtual ~JobBase() = default;
    };

    template <typename Func, typename IndexFunc>
    struct Job : public JobBase {
        alignas(64) std::atomic<size_t> current_chunk{0};
        size_t count;
        size_t chunk_size;
        size_t total_chunks;
        Func func;
        bool has_chunk_id;

        Job(size_t c, size_t cs, size_t tc, Func&& f, bool chunked)
            : count(c), chunk_size(cs), total_chunks(tc), func(std::forward<Func>(f)), has_chunk_id(chunked) {}

        void execute_chunk() override {
            size_t chunk_id = current_chunk.fetch_add(1, std::memory_order_relaxed);
            if (chunk_id < total_chunks) {
                size_t start = chunk_id * chunk_size;
                size_t end = std::min(start + chunk_size, count);

                for (size_t i = start; i < end; ++i) {
                    if constexpr (std::is_invocable_v<Func, size_t, size_t>) {
                        func(i, chunk_id);
                    } else {
                        func(i);
                    }
                }
            }
        }

        bool is_finished() const override {
            return current_chunk.load(std::memory_order_relaxed) >= total_chunks;
        }
    };
    
    struct alignas(64) Worker {
        std::thread thread;
        std::atomic<bool> active{true};
        std::atomic<bool> has_task{false};
        std::function<void()> task;
    };

    std::vector<Worker> workers;
    std::atomic<bool> stop_all{false};
    
    void worker_loop()
    {
        int idle_counter = 0;
        while (true)
        {
            // 1. Snelle check op actieve parallel jobs (hot path)
            JobBase* current = active_job.load(std::memory_order_acquire);
            if (current != nullptr) {
                idle_counter = 0; // Reset direct zodra er werk is
                if (!current->is_finished()) {
                    current->execute_chunk();
                    continue;
                }
            }

            // 2. HYBRID SPIN-WAIT: Voorkom dat threads direct naar de OS kernel slapen gaan.
            // Geef ze een korte kans om te wachten op de volgende parallel_for in dezelfde frame.
            if (idle_counter < 2000) {
                idle_counter++;
                _mm_pause();
                continue;
            }

            // 3. Pas na langere inactiviteit vallen we terug op de OS-niveau condition_variable
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                condition.wait(lock, [this] { 
                    return stop.load(std::memory_order_release) || task_head < tasks.size() || active_job.load(std::memory_order_relaxed) != nullptr; 
                });

                if (stop.load(std::memory_order_acquire) && task_head == tasks.size()) {
                    return; 
                }

                if (active_job.load(std::memory_order_relaxed) != nullptr) {
                    idle_counter = 0;
                    continue; 
                }

                if (task_head < tasks.size()) {
                    task = std::move(tasks[task_head++]);
                    if (task_head == tasks.size()) {
                        tasks.clear();
                        task_head = 0;
                    }
                    idle_counter = 0;
                }
            } 

            if (task) {
                activeThreads.fetch_add(1, std::memory_order_relaxed);
                task(); 
                activeThreads.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }

    std::vector<std::function<void()>> tasks;
    size_t task_head = 0;

    alignas(64) std::mutex queue_mutex;
    std::condition_variable condition;

    alignas(64) std::atomic<bool> stop{false};
    alignas(64) std::atomic<int> activeThreads{0};
    
    // Lock-free communicatiekanaal voor parallel_for (0 OS context switches)
    alignas(64) std::atomic<JobBase*> active_job{nullptr};
};