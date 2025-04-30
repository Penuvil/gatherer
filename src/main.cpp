#include <atomic>
#include <condition_variable>
#include <functional>
#include <queue>
#include <thread>

#include "toml.hpp"
#include "entt/entt.hpp"

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

  inline bool poll() {
    return this->m_ready.load(std::memory_order_acquire);
  }

private:
  std::atomic<T> m_result;
  std::atomic<bool> m_ready;
};

template <typename T> class LightPromise {
public:
  inline LightFuture<T> *get_future() {
    return &this->m_future;
  }

  inline void set_value(T value) {
    m_future.set(value);
  }

private:
  LightFuture<T> m_future;
};

struct Context {
  int width;
  int height;
};

void init(Context &ctx) {
  auto config = toml::parse_file("resources/config.toml");
  ctx.width = config["window"]["width"].node()->as_integer()->get();
  ctx.height = config["window"]["height"].node()->as_integer()->get();
}

struct ThreadPool {
  std::queue<std::function<void()>> tasks;
  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
  std::vector<std::thread> workers;

  ThreadPool(size_t num_threads) : stop(false) {
    for (size_t i = 0; i < num_threads; i++) {
      workers.emplace_back([this] {
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->condition.wait(
                lock, [this] { return this->stop || !this->tasks.empty(); });
            if (this->stop && this->tasks.empty())
              return;
            task = std::move(this->tasks.front());
            this->tasks.pop();
          }
          task();
        }
      });
    }
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers) {
      worker.join();
    }
  }
};

void task_submit(ThreadPool &pool, std::function<void()> task) {
  {
    std::unique_lock<std::mutex> lock(pool.queue_mutex);
    pool.tasks.push(task);
  }
  pool.condition.notify_one();
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
  auto pool = gatherer::ThreadPool(4);
  entt::dispatcher dispatcher;

  dispatcher.sink<InputEvent>().connect<&on_input_event>();
  dispatcher.sink<PhysicsEvent>().connect<&on_physics_event>();

  gatherer::LightPromise<int> promise;
  auto *future = promise.get_future();

  gatherer::task_submit(pool, [&promise, &dispatcher] {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    promise.set_value(42);
    dispatcher.enqueue<InputEvent>(InputEvent{65});
    dispatcher.enqueue<PhysicsEvent>(PhysicsEvent{0.016f});
    printf("Task 1 executed on thread %s\n", oss.str().c_str());
  });

  gatherer::task_submit(pool, [&dispatcher] {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    dispatcher.enqueue<InputEvent>(InputEvent{66});
    printf("Task 2 executed on thread %s\n", oss.str().c_str());
  });

  bool running = true;
  int frame_count = 0;
  while(running) {

    dispatcher.update();

//    printf("Main loop frame %i\n", frame_count++);
    frame_count++;
    std::this_thread::sleep_for(std::chrono::milliseconds(16));

    if (frame_count > 100) {
      running = false;
    }
  }
  int result = future->get();

  printf("Promise result: %i\n", result);

  gatherer::Context ctx = {};
  init(ctx);
  printf("Hello world!\n");
  printf("Window: Width: %d, Height: %d\n", ctx.width, ctx.height);

  return EXIT_SUCCESS;
}
