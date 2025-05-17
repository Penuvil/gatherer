#include <atomic>
#include <queue>
#include <thread>
#include <functional>

#include <SDL3/SDL_mutex.h>

namespace gatherer {

template <typename T>
concept AtomicCompatible =
    std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>;

template <AtomicCompatible T> class LightFuture {
public:
  LightFuture() : m_result(), m_ready(false) {}
  ~LightFuture() = default;

  inline void set(T result) {
    this->m_result.store(result, std::memory_order_release);
    this->m_ready.store(true, std::memory_order_release);
    return;
  }

  inline T get() {
    while (!this->m_ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    return this->m_result.load(std::memory_order_acquire);
  }

  inline bool poll() { return this->m_ready.load(std::memory_order_acquire); }

private:
  std::atomic<T> m_result;
  std::atomic<bool> m_ready;
};

template <typename T> class LightPromise {
public:
  inline LightFuture<T> *get_future() { return &this->m_future; }

  inline void set_value(T value) { m_future.set(value); }

private:
  LightFuture<T> m_future;
};

struct ThreadPool {
  std::queue<std::function<void()>> tasks;
  SDL_Mutex *queue_mutex;
  SDL_Condition *condition;
  bool stop;
  std::vector<SDL_Thread *> workers;

  static int worker(void *ptr) {
    auto data = (ThreadPool *)ptr;
    while (true) {
      std::function<void()> task;

      SDL_LockMutex(data->queue_mutex);
      SDL_WaitCondition(data->condition, data->queue_mutex);
      if (data->stop && data->tasks.empty())
        return 0;
      task = std::move(data->tasks.front());
      data->tasks.pop();
      SDL_UnlockMutex(data->queue_mutex);

      task();
    }
  }

  ThreadPool(size_t num_threads)
      : queue_mutex(SDL_CreateMutex()), condition(SDL_CreateCondition()),
        stop(false) {
    for (size_t i = 0; i < num_threads; i++) {
      SDL_CreateThread(worker, "", (void *)this);
    }
  }

  ~ThreadPool() {

    SDL_LockMutex(queue_mutex);
    stop = true;
    SDL_UnlockMutex(queue_mutex);

    SDL_BroadcastCondition(condition);
    for (SDL_Thread *worker : workers) {
      int status;
      SDL_WaitThread(worker, &status);
    }
    SDL_DestroyCondition(condition);
    SDL_DestroyMutex(queue_mutex);
  }
};

void task_submit(ThreadPool &pool, std::function<void()> task) {

  SDL_LockMutex(pool.queue_mutex);
  pool.tasks.push(task);
  SDL_UnlockMutex(pool.queue_mutex);
  SDL_SignalCondition(pool.condition);
}
} // namespace gatherer
