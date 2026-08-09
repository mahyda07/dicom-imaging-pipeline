#pragma once
// A thread pool built from raw primitives (std::thread, std::mutex,
// std::condition_variable) rather than std::async, since the internship
// spec specifically asks for a custom pool — the point is demonstrating
// the synchronization design, not just calling a library that hides it.

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <vector>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount) : stopping_(false) {
        for (size_t i = 0; i < threadCount; i++) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    // Non-copyable, non-movable — a pool owns live threads, copying it
    // would leave two objects thinking they own the same worker threads.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = true;
        }
        // Wake every worker so they notice `stopping_` and exit their loop,
        // rather than sleeping forever waiting on a queue that will never
        // get more work.
        condition_.notify_all();
        for (std::thread& worker : workers_) worker.join();
    }

    // Submits a task and returns a future for its result. The caller can
    // ignore the future (fire-and-forget) or call .get() to block until
    // this specific task finishes and retrieve its return value.
    template <typename Fn, typename... Args>
    auto submit(Fn&& fn, Args&&... args) -> std::future<std::invoke_result_t<Fn, Args...>> {
        using ReturnType = std::invoke_result_t<Fn, Args...>;

        // packaged_task wraps the callable so its result can be retrieved
        // later via the future, even though the actual call happens on a
        // different thread at an unknown later time.
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
        std::future<ReturnType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) throw std::runtime_error("submit() called on a stopped ThreadPool");
            tasks_.emplace([task] { (*task)(); });
        }
        condition_.notify_one(); // wake exactly one sleeping worker to take this task
        return result;
    }

    size_t threadCount() const { return workers_.size(); }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                // Sleep here (not busy-wait/spin) until either a task
                // arrives or the pool is being destroyed. condition_variable
                // handles the "sleep until woken" part correctly, including
                // re-checking the condition to guard against spurious wakeups.
                condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });

                if (stopping_ && tasks_.empty()) return; // no more work, and shutting down

                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task(); // run the task OUTSIDE the lock — other workers can pull
                    // their own tasks while this one is actually executing
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    bool stopping_;
};
