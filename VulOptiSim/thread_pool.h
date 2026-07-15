#pragma once

//You can use this threadpool class in your code to reduce the overhead of spawning threads.
//  Unfortunately it's missing a notification mechanism (e.g. waking up a worker when a task is pushed). 
//  To use this thread pool you need to fix this issue before the threadpool works.
//  Discuss with your fellow students how this can be done. :)
// Update: fixed

class ThreadPool; //Forward declare

class Worker
{
public:
    //Instantiate the worker class by passing and storing the threadpool as a reference
    explicit Worker(ThreadPool& s) : pool(s) {}

    void operator()();

private:
    ThreadPool& pool;
};

class ThreadPool
{
public:
    explicit ThreadPool(size_t numThreads)
        : activeThreads(0) // initialiseer actieve threads counter op 0
    {
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers.push_back(std::thread(Worker(*this)));
        }
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true; // stop all threads
        }

        condition.notify_all(); // Wek alle threads zodat ze kunnen stoppen

        for (auto& thread : workers) {
            thread.join();
        }
    }


    template<typename Func>
    void parallel_for(size_t count, Func&& func)
        {
            if(count == 0)
                return;

            const size_t thread_count = workers.size();

            // Kleine arrays gewoon single-thread
            if(count < thread_count * 4)
            {
                for(size_t i = 0; i < count; i++)
                    func(i);

                return;
            }


            size_t chunk_size = (count + thread_count - 1) / thread_count;

            std::vector<std::future<void>> futures;
            futures.reserve(thread_count);


            for(size_t start = 0; start < count; start += chunk_size)
            {
                size_t end = std::min(start + chunk_size, count);


                futures.push_back(
                    enqueue([&, start, end]()
                    {
                        for(size_t i = start; i < end; i++)
                        {
                            func(i);
                        }
                    })
                );
            }


            // Wacht tot alle blokken klaar zijn
            for(auto& future : futures)
            {
                future.get();
            }
        }
    
    template <class T>
    [[nodiscard]] auto enqueue(T task) -> std::future<decltype(task())>
    {
        //Wrap the function in a packaged_task so we can return a future object
        auto wrapper = std::make_shared<std::packaged_task<decltype(task())()>>(std::move(task));

        //Scope to restrict critical section
        {
            //lock our queue and add the given task to it
            std::unique_lock<std::mutex> lock(queue_mutex);

            tasks.push_back([=] {(*wrapper)(); });

            condition.notify_one(); // Wek 1 worker
        }


        return wrapper->get_future();
    }

    void printActiveThreads() const // om te zien hoeveel threads actief zijn, roep in een functie op met pool.printActiveThreads()
    {
        std::cout << "Active threads: " << activeThreads << std::endl;
    }


private:
    friend class Worker; //Gives access to the private variables of this class

    std::vector<std::thread> workers;
    std::deque<std::function<void()>> tasks;
    std::condition_variable condition; // Wek threads op bij werk

    std::mutex queue_mutex; //Lock for our queue
    std::atomic<bool> stop = false;
    std::atomic<int> activeThreads;  // Bijhouden van actieve threads, atomic want anti race

};

inline void Worker::operator()()
{
    while (true)
    {
        //Scope to restrict critical section
        //This is important because we don't want to hold the lock while executing the task,
        //because that would make it so only one task can be run simultaneously (aka sequantial)
        std::function<void()> task;  // task declareren 

        {   // Scoped lock zodat hij zo kort mogelijk wordt vastgehouden
            std::unique_lock<std::mutex> lock(pool.queue_mutex);

            // Wacht tot er werk is of de pool stopt
            pool.condition.wait(lock, [&] { return pool.stop || !pool.tasks.empty(); });

            if (pool.stop && pool.tasks.empty()) {
                lock.unlock(); // Unlock als er geen werk meer is
                return;  // Stop als er geen werk meer is
            }

            task = std::move(pool.tasks.front()); // Haal taak op
            pool.tasks.pop_front();
        }  // Lock wordt hier vrijgegeven


        // Verhoog het aantal actieve threads voordat de taak begint
        pool.activeThreads.fetch_add(1, std::memory_order_relaxed);

        task(); // Voer de taak uit

        // Verlaag het aantal actieve threads nadat de taak is uitgevoerd
        pool.activeThreads.fetch_sub(1, std::memory_order_relaxed);

    }


}