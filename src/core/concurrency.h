#pragma once

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace Core {

/* Reader-Writer locked wrapper. Provides safe concurrent read access
 * with exclusive write access using std::shared_mutex.
 */
template<typename T>
class RWLocked {
public:
  RWLocked() = default;
  explicit RWLocked(T value) : data_(std::move(value)) {}

  /* Read access: multiple readers allowed concurrently */
  template<typename Func>
  auto read(Func&& func) const -> decltype(func(std::declval<const T&>())) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return func(data_);
  }

  /* Write access: exclusive */
  template<typename Func>
  auto write(Func&& func) -> decltype(func(std::declval<T&>())) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return func(data_);
  }

  /* Get a snapshot (copy) of the data under read lock */
  T snapshot() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return data_;
  }

  std::shared_mutex& mutex() const { return mutex_; }

private:
  T data_;
  mutable std::shared_mutex mutex_;
};

/* Thread-safe shared_ptr wrapper using atomic operations */
template<typename T>
class AtomicSharedPtr {
public:
  AtomicSharedPtr() = default;
  explicit AtomicSharedPtr(std::shared_ptr<T> ptr) : ptr_(std::move(ptr)) {}

  std::shared_ptr<T> load() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ptr_;
  }

  void store(std::shared_ptr<T> desired) {
    std::lock_guard<std::mutex> lock(mutex_);
    ptr_ = std::move(desired);
  }

  std::shared_ptr<T> exchange(std::shared_ptr<T> desired) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::shared_ptr<T> old = std::move(ptr_);
    ptr_ = std::move(desired);
    return old;
  }

private:
  std::shared_ptr<T> ptr_;
  mutable std::mutex mutex_;
};

/* SpinLock for very short critical sections where mutex overhead
 * would dominate. Uses atomic flag with exponential backoff.
 */
class SpinLock {
public:
  void lock() {
    while (flag_.test_and_set(std::memory_order_acquire)) {
      // Spin with CPU hint
#if defined(__x86_64__) || defined(__i386__)
      __builtin_ia32_pause();
#endif
    }
  }

  void unlock() {
    flag_.clear(std::memory_order_release);
  }

  bool try_lock() {
    return !flag_.test_and_set(std::memory_order_acquire);
  }

private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

} // namespace Core
