#include <gtest/gtest.h>
#include "thread_safe_queue.h"

TEST(ThreadSafeQueue, PushPop)
{
    util::Queue<int> queue(10);

    ASSERT_FALSE(queue.full());
    ASSERT_TRUE(queue.empty());

    for (int i = 0; i < 10; ++i)
        queue.push(int(i));

    ASSERT_TRUE(queue.full());
    ASSERT_FALSE(queue.empty());

    for (int i = 0; i < 10; ++i)
    {
        auto item = queue.pop();
        ASSERT_TRUE(item.has_value());
        ASSERT_EQ(item.value(), i);
    }

    ASSERT_FALSE(queue.full());
    ASSERT_TRUE(queue.empty());
    ASSERT_FALSE(queue.try_pop().has_value());
}

TEST(ThreadSafeQueue, TaskDone)
{
    util::Queue<int> queue(10);
    std::atomic<bool> workerDone = false;

    for (int i = 0; i < 10; ++i)
        queue.push(int(i));

    std::thread worker([&queue, &workerDone]()
    {
        for (int i = 0; i < 10; ++i)
        {
            auto item = queue.pop();
            ASSERT_TRUE(item.has_value());
            ASSERT_EQ(item.value(), i);
            if (queue.empty())
                workerDone = true;
            queue.task_done();
        }
    });

    queue.join();
    ASSERT_TRUE(workerDone);
    worker.join();
}
