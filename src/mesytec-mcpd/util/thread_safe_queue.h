#ifndef B6CDDCF7_5AAB_4C17_8369_5BE4C0B95678
#define B6CDDCF7_5AAB_4C17_8369_5BE4C0B95678

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace util
{

// From https://morestina.net/1400/minimalistic-blocking-bounded-queue-for-c
// Modified pop and try_pop to return std::optional instead of taking a
// reference as this is for c++17. Renamed from 'queue' to 'Queue'.
// Added task_done(), join(), size(), empty(), full()
template <typename T> class Queue
{
    std::deque<T> content;
    size_t capacity;

    std::mutex mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    std::condition_variable all_tasks_done;
    size_t unfinished_tasks = 0;

    Queue(const Queue &) = delete;
    Queue(Queue &&) = delete;
    Queue &operator=(const Queue &) = delete;
    Queue &operator=(Queue &&) = delete;

  public:
    Queue(size_t capacity)
        : capacity(capacity)
    {
    }

    void push(T &&item)
    {
        {
            std::unique_lock<std::mutex> lk(mutex);
            not_full.wait(lk, [this]() { return content.size() < capacity; });
            content.push_back(std::move(item));
            ++unfinished_tasks;
        }
        not_empty.notify_one();
    }

    bool try_push(T &&item)
    {
        {
            std::unique_lock<std::mutex> lk(mutex);
            if (content.size() == capacity)
                return false;
            content.push_back(std::move(item));
        }
        not_empty.notify_one();
        return true;
    }

    std::optional<T> pop()
    {
        std::optional<T> result;
        {
            std::unique_lock<std::mutex> lk(mutex);
            not_empty.wait(lk, [this]() { return !content.empty(); });
            result = std::move(content.front());
            content.pop_front();
        }
        not_full.notify_one();
        return result;
    }

    std::optional<T> try_pop()
    {
        std::optional<T> result;
        std::unique_lock<std::mutex> lk(mutex);
        if (!content.empty())
        {
            result = std::move(content.front());
            content.pop_front();
        }
        not_full.notify_one();
        return result;
    }

    void task_done()
    {
        std::unique_lock<std::mutex> lk(mutex);
        if (unfinished_tasks == 0)
            throw std::runtime_error("task_done() called too many times");
        if (--unfinished_tasks == 0)
            all_tasks_done.notify_all();
    }

    void join()
    {
        std::unique_lock<std::mutex> lk(mutex);
        all_tasks_done.wait(lk, [this]() { return unfinished_tasks == 0; });
    }

    size_t size()
    {
        std::unique_lock<std::mutex> lk(mutex);
        return content.size();
    }

    bool empty()
    {
        std::unique_lock<std::mutex> lk(mutex);
        return content.empty();
    }

    bool full()
    {
        std::unique_lock<std::mutex> lk(mutex);
        return content.size() >= capacity;
    }
};

} // namespace util

#endif /* B6CDDCF7_5AAB_4C17_8369_5BE4C0B95678 */
