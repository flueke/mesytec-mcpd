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
template <typename T> class Queue
{
    std::deque<T> content;
    size_t capacity;

    std::mutex mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;

    queue(const queue &) = delete;
    queue(queue &&) = delete;
    queue &operator=(const queue &) = delete;
    queue &operator=(queue &&) = delete;

  public:
    queue(size_t capacity)
        : capacity(capacity)
    {
    }

    void push(T &&item)
    {
        {
            std::unique_lock<std::mutex> lk(mutex);
            not_full.wait(lk, [this]() { return content.size() < capacity; });
            content.push_back(std::move(item));
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
        {
            std::optional<T> result;
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
};

} // namespace util

#endif /* B6CDDCF7_5AAB_4C17_8369_5BE4C0B95678 */
