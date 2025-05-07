#include <atomic>
#include <queue>
#include <thread>

#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "entt/entt.hpp"
#include "toml.hpp"
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

struct Context {
  int width;
  int height;
  SDL_Window *window;
};

void init(Context &ctx) {
  SDL_Init(0);
  auto config = toml::parse_file("resources/config.toml");
  ctx.width = config["window"]["width"].node()->as_integer()->get();
  ctx.height = config["window"]["height"].node()->as_integer()->get();
  ctx.window = SDL_CreateWindow("Gatherer", ctx.width, ctx.height, 0);
}

struct ThreadPool {
  std::queue<std::function<void()>> tasks;
  SDL_Mutex *queue_mutex;
  SDL_Condition *condition;
  bool stop;
  std::vector<SDL_Thread *> workers;

  ThreadPool(size_t num_threads)
      : queue_mutex(SDL_CreateMutex()), condition(SDL_CreateCondition()),
        stop(false) {
    int worker(void *ptr);
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

int worker(void *ptr) {
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

void task_submit(ThreadPool &pool, std::function<void()> task) {

  SDL_LockMutex(pool.queue_mutex);
  pool.tasks.push(task);
  SDL_UnlockMutex(pool.queue_mutex);
  SDL_SignalCondition(pool.condition);
}
} // namespace gatherer

struct InputEvent {
  int key_code;
};

struct PhysicsEvent {
  float delta_time;
};

void on_input_event(const InputEvent &event) {
  printf("Received InputEvent: key_code = %i\n", event.key_code);
}

void on_physics_event(const PhysicsEvent &event) {
  printf("Received PhysicsEvent: delta_timne = %f\n", event.delta_time);
}

int main() {
  gatherer::Context ctx = {};
  init(ctx);
  printf("Hello world!\n");
  printf("Window: Width: %d, Height: %d\n", ctx.width, ctx.height);

  auto pool = gatherer::ThreadPool(4);
  entt::dispatcher dispatcher;

  dispatcher.sink<InputEvent>().connect<&on_input_event>();
  dispatcher.sink<PhysicsEvent>().connect<&on_physics_event>();

  gatherer::LightPromise<int> promise;
  auto *future = promise.get_future();

  gatherer::task_submit(pool, [&promise, &dispatcher] {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    promise.set_value(42);
    dispatcher.enqueue<InputEvent>(InputEvent{65});
    dispatcher.enqueue<PhysicsEvent>(PhysicsEvent{0.016f});
    printf("Task 1 executed on thread %lu\n", SDL_GetCurrentThreadID());
  });

  gatherer::task_submit(pool, [&dispatcher] {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    dispatcher.enqueue<InputEvent>(InputEvent{66});
    printf("Task 2 executed on thread %lu\n", SDL_GetCurrentThreadID());
  });

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_KEY_DOWN:
        switch (event.key.key) {
        case SDLK_Q:
          running = false;
          break;
        default:
          break;
        }
        break;
      default:
        break;
      }
    }

    dispatcher.update();

    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  int result = future->get();

  printf("Promise result: %i\n", result);
  SDL_DestroyWindow(ctx.window);
  return EXIT_SUCCESS;
}
