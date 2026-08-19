#include "thread_pool.h"
#include <iostream>
#include <atomic>
#include <vector>
#include <cassert>

int main() {
    int failures = 0;

    {
        ThreadPool pool(4);
        std::atomic<int> counter{0};
        std::vector<std::future<void>> futures;
        for (int i = 0; i < 1000; i++) futures.push_back(pool.submit([&counter] { counter++; }));
        for (auto& f : futures) f.get();

        bool pass = (counter.load() == 1000);
        std::cout << "Test 1 (1000 tasks all run exactly once): "
                  << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) failures++;
    }

    {
        ThreadPool pool(4);
        auto f1 = pool.submit([] { return 10; });
        auto f2 = pool.submit([] { return 20; });
        int sum = f1.get() + f2.get();

        bool pass = (sum == 30);
        std::cout << "Test 2 (future return values correct): " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) failures++;
    }

    {
        ThreadPool pool(6);
        bool pass = (pool.threadCount() == 6);
        std::cout << "Test 3 (thread count matches request): " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) failures++;
    }

    {
        ThreadPool pool(2);
        for (int i = 0; i < 50; i++) pool.submit([] {
            volatile int x = 0;
            for (int j = 0; j < 1000; j++) x += j;
        });
    }
    std::cout << "Test 4 (clean shutdown with pending work, no hang): PASS\n";

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : std::to_string(failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
