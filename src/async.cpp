#include <coroutine>
#include <expected>

#include <SDL3/SDL_thread.h>
#include <atomic>
#include <functional>
#include <future>
#include <queue>
#include <thread>

#include "gatherer.hpp"
#include <SDL3/SDL_mutex.h>

namespace gatherer {

template <typename T>
concept AtomicCompatible =
    std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>;

template <typename T> class LightFuture {
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
      auto thread = SDL_CreateThread(worker, "", (void *)this);
      workers.push_back(thread);
      SDL_DetachThread(thread);
    }
  }

  ~ThreadPool() {
    SDL_LockMutex(queue_mutex);
    stop = true;
    SDL_UnlockMutex(queue_mutex);
    SDL_BroadcastCondition(condition);
    SDL_DestroyCondition(condition);
    SDL_DestroyMutex(queue_mutex);
  }
};

void task_submit(ThreadPool *pool, std::function<void()> task) {

  SDL_LockMutex(pool->queue_mutex);
  pool->tasks.push(task);
  SDL_UnlockMutex(pool->queue_mutex);
  SDL_SignalCondition(pool->condition);
}

template <typename T> struct Task {
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  handle_type coro;

  explicit Task(handle_type h) : coro(h) {}
  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;
  Task(Task &&other) noexcept : coro(other.coro) { other.coro = nullptr; }
  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (coro)
        coro.destroy();
      coro = other.coro;
      other.coro = nullptr;
    }
    return *this;
  }
  ~Task() {
    if (coro)
      coro.destroy();
  }

  struct Awaiter {
    handle_type coro;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
      coro.promise().continuation = awaiting;
      coro.resume();
    }
    std::expected<T, std::string> await_resume() noexcept {
      return coro.promise().result;
    }
  };

  auto operator co_await() noexcept {
    auto tmp = coro;
    coro = nullptr;
    return Awaiter{tmp};
  }

  struct promise_type {
    std::expected<T, std::string> result;
    std::coroutine_handle<> continuation = nullptr;

    auto get_return_object() { return Task{handle_type::from_promise(*this)}; }
    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      bool await_ready() noexcept { return false; }
      void await_suspend(handle_type handle) noexcept {
        if (handle.promise().continuation)
          handle.promise().continuation.resume();
      }
      void await_resume() noexcept {}
    };
    auto final_suspend() noexcept { return FinalAwaiter{}; }

    template <typename U> void return_value(U &&value) noexcept {
      result = std::expected<T, std::string>{std::forward<U>(value)};
    }

    void unhandled_exception() noexcept {
      try {
        throw;
      } catch (const std::exception &e) {
        result = std::unexpected(std::string(e.what()));
      } catch (...) {
        result = std::unexpected("unknown exception");
      }
    }
  };
};

template <> struct Task<void> {
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  handle_type coro;

  explicit Task(handle_type h) : coro(h) {}
  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;
  Task(Task &&other) noexcept : coro(other.coro) { other.coro = nullptr; }
  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (coro)
        coro.destroy();
      coro = other.coro;
      other.coro = nullptr;
    }
    return *this;
  }
  ~Task() {
    if (coro)
      coro.destroy();
  }

  struct Awaiter {
    handle_type coro;
    Context *ctx;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
      coro.promise().continuation = awaiting;
      task_submit(ctx->pool, [h = coro]() { h.resume(); });
    }
    std::expected<void, std::string> await_resume() noexcept {
      return coro.promise().result;
    }
  };

  auto operator co_await() noexcept {
    auto tmp = coro;
    coro = nullptr;
    return Awaiter{tmp, tmp.promise().ctx};
  }

  struct promise_type {
    std::expected<void, std::string> result;
    std::coroutine_handle<> continuation = nullptr;
    Context *ctx;

    promise_type(Context *ctx) : ctx(ctx) {}

    auto get_return_object() { return Task{handle_type::from_promise(*this)}; }
    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      Context *ctx;
      bool await_ready() noexcept { return false; }
      void await_suspend(handle_type handle) noexcept {
        if (handle.promise().continuation)
          task_submit(ctx->pool, [cont = handle.promise().continuation]() {
            cont.resume();
          });
      }
      void await_resume() noexcept {}
    };
    auto final_suspend() noexcept { return FinalAwaiter{ctx}; }

    void return_void() noexcept { result = std::expected<void, std::string>{}; }

    void unhandled_exception() noexcept {
      try {
        throw;
      } catch (const std::exception &e) {
        result = std::unexpected(std::string(e.what()));
      } catch (...) {
        result = std::unexpected("unknown exception");
      }
    }
  };
};
} // namespace gatherer
