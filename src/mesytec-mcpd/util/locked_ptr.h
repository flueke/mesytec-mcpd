#ifndef A0A03986_35C1_4220_B2E7_1A945486DDAB
#define A0A03986_35C1_4220_B2E7_1A945486DDAB

#include <mutex>
#include <utility>

namespace mesytec::mcpd
{

template <class T, class Mutex = std::mutex> class locked_ptr
{
  public:
    locked_ptr() = default;

    template <class... Args>
    explicit locked_ptr(std::in_place_t, Args &&...args)
        : obj_(std::forward<Args>(args)...)
    {
    }

    // Non-copyable, movable owner
    locked_ptr(const locked_ptr &) = delete;
    locked_ptr &operator=(const locked_ptr &) = delete;

    locked_ptr(locked_ptr &&other) noexcept
    {
        std::scoped_lock lock(other.mtx_);
        obj_ = std::move(other.obj_);
    }

    locked_ptr &operator=(locked_ptr &&other) noexcept
    {
        if (this != &other)
        {
            // Avoid self-deadlock with std::scoped_lock on both mutexes
            std::scoped_lock lock(mtx_, other.mtx_);
            obj_ = std::move(other.obj_);
        }
        return *this;
    }

    // Guard type: locks in ctor, unlocks in dtor, exposes T via operator-> / operator*
    class guard
    {
      public:
        guard(T &obj, Mutex &mtx)
            : obj_(obj)
            , lock_(mtx)
        {
        }

        T *operator->() { return &obj_; }
        T &operator*() { return obj_; }

        // Non-copyable, movable if needed
        guard(const guard &) = delete;
        guard &operator=(const guard &) = delete;

        guard(guard &&) = default;
        guard &operator=(guard &&) = default;

      private:
        T &obj_;
        std::unique_lock<Mutex> lock_;
    };

    // Get a guard; scope determines lock lifetime
    guard lock() { return guard{obj_, mtx_}; }

  private:
    T obj_{};
    mutable Mutex mtx_;
};

} // namespace mesytec::mcpd

#endif /* A0A03986_35C1_4220_B2E7_1A945486DDAB */
