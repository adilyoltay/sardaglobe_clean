// BoundedQueue Test
// Tests thread-safe bounded queue with non-blocking operations

#include "../src/core/bounded_queue.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

using namespace globe;

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

int main() {
    int failed = 0;
    std::cout << "=== BoundedQueue Test ===\n";

    // Test 1: Basic push/pop
    {
        BoundedQueue<int> q(3);
        
        failed += !Expect(q.Push(1), "Push should succeed on empty queue");
        failed += !Expect(q.Push(2), "Push should succeed with space");
        
        int val;
        failed += !Expect(q.TryPop(val), "TryPop should succeed");
        failed += !Expect(val == 1, "First popped should be 1");
        
        std::cout << "  Basic push/pop: OK\n";
    }

    // Test 2: Full queue behavior
    {
        BoundedQueue<int> q(2);
        
        failed += !Expect(q.Push(1), "Push 1 should succeed");
        failed += !Expect(q.Push(2), "Push 2 should succeed");
        
        // TryPush should fail when full
        bool tryPushResult = q.TryPush(3);
        failed += !Expect(!tryPushResult, "TryPush should fail on full queue");
        
        // Pop to make space
        int val;
        failed += !Expect(q.TryPop(val), "TryPop should succeed");
        
        // Now TryPush should succeed
        failed += !Expect(q.TryPush(3), "TryPush should succeed after pop");
        
        std::cout << "  Full queue behavior: OK\n";
    }

    // Test 3: Empty queue behavior
    {
        BoundedQueue<int> q(3);
        
        int val;
        failed += !Expect(!q.TryPop(val), "TryPop should fail on empty queue");
        
        std::cout << "  Empty queue behavior: OK\n";
    }

    // Test 4: Non-blocking nature of TryPush
    {
        BoundedQueue<int> q(1);
        failed += !Expect(q.TryPush(1), "Initial TryPush should succeed");
        
        // TryPush should return immediately with false, not block
        auto start = std::chrono::steady_clock::now();
        bool result = q.TryPush(2);
        auto end = std::chrono::steady_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        failed += !Expect(!result, "TryPush should return false on full queue");
        failed += !Expect(elapsed.count() < 10, "TryPush should not block (was < 10ms)");
        
        std::cout << "  Non-blocking TryPush: OK (" << elapsed.count() << "ms)\n";
    }

    // Test 5: Producer-consumer pattern
    {
        BoundedQueue<int> q(10);
        std::atomic<int> sum{0};
        std::atomic<int> count{0};
        std::atomic<bool> producerDone{false};
        
        // Producer thread
        std::thread producer([&]() {
            for (int i = 1; i <= 100; ++i) {
                // Use blocking Push for simplicity in test
                q.Push(i);
            }
            producerDone.store(true);
            q.Close();  // Signal no more items
        });
        
        // Consumer thread
        std::thread consumer([&]() {
            int val;
            while (true) {
                if (q.TryPop(val)) {
                    sum += val;
                    count++;
                } else if (producerDone.load() && !q.TryPop(val)) {
                    // Queue is empty and producer is done
                    break;
                }
            }
        });
        
        producer.join();
        consumer.join();
        
        // Sum of 1 to 100 = 5050
        failed += !Expect(sum == 5050, "Sum should be 5050");
        failed += !Expect(count == 100, "Count should be 100");
        
        std::cout << "  Producer-consumer: OK (sum=" << sum << ", count=" << count << ")\n";
    }

    // Test 6: TryPush with move semantics
    {
        struct LargeStruct {
            std::vector<int> data;
            LargeStruct() : data(1000, 42) {}
        };
        
        BoundedQueue<LargeStruct> q(2);
        
        // TryPush with move
        LargeStruct item1, item2, item3;
        failed += !Expect(q.TryPush(std::move(item1)), "TryPush with move should succeed");
        failed += !Expect(q.TryPush(std::move(item2)), "TryPush with move should succeed");
        failed += !Expect(!q.TryPush(std::move(item3)), "TryPush should fail on full queue");
        
        std::cout << "  TryPush move semantics: OK\n";
    }

    // Test 7: Clear and Close behavior
    {
        BoundedQueue<int> q(3);
        q.Push(1);
        q.Push(2);
        
        q.Close();
        
        // TryPop should fail when closed and empty
        int val;
        failed += !Expect(q.TryPop(val), "TryPop should succeed before empty");
        failed += !Expect(q.TryPop(val), "TryPop should succeed before empty");
        failed += !Expect(!q.TryPop(val), "TryPop should fail when closed and empty");
        
        // TryPush should fail when closed
        failed += !Expect(!q.TryPush(3), "TryPush should fail when closed");
        
        std::cout << "  Close behavior: OK\n";
    }

    // Test 8: Stress test - many threads
    {
        BoundedQueue<int> q(100);
        std::atomic<int> pushed{0};
        std::atomic<int> popped{0};
        
        std::vector<std::thread> threads;
        
        // Multiple producer threads using TryPush
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&q, &pushed]() {
                for (int i = 0; i < 100; ++i) {
                    while (!q.TryPush(i)) {
                        // Spin until push succeeds
                        std::this_thread::yield();
                    }
                    pushed++;
                }
            });
        }
        
        // Multiple consumer threads
        for (int t = 0; t < 2; ++t) {
            threads.emplace_back([&q, &popped]() {
                int val;
                for (int i = 0; i < 200; ++i) {
                    while (!q.TryPop(val)) {
                        std::this_thread::yield();
                    }
                    popped++;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        failed += !Expect(pushed == 400, "Should have pushed 400 items");
        failed += !Expect(popped == 400, "Should have popped 400 items");
        
        std::cout << "  Stress test: OK (pushed=" << pushed << ", popped=" << popped << ")\n";
    }

    if (failed == 0) {
        std::cout << "BoundedQueueTest PASSED\n";
        return 0;
    }

    std::cerr << "BoundedQueueTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
