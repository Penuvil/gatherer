#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "entt/entt.hpp"
#include "toml.hpp"

#include "assets.cpp"
#include "async.cpp"
#include <SDL3/SDL_gpu.h>

namespace gatherer {

struct Context {
  int width;
  int height;
  SDL_Window *window;
  SDL_GPUDevice *device;
};

void init(Context &ctx) {
  SDL_Init(0);
  auto config = toml::parse_file("resources/config.toml");
  ctx.width = config["window"]["width"].node()->as_integer()->get();
  ctx.height = config["window"]["height"].node()->as_integer()->get();
  ctx.window = SDL_CreateWindow("Gatherer", ctx.width, ctx.height, 0);
  ctx.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
  SDL_ClaimWindowForGPUDevice(ctx.device, ctx.window);
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

int main(int argc, char **argv) {
  gatherer::Context ctx = {};
  init(ctx);
  printf("Hello world!\n");
  printf("Window: Width: %d, Height: %d\n", ctx.width, ctx.height);

  gatherer::AssetManager asset_manager = gatherer::AssetManager();

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

  gatherer::task_submit(pool, [&ctx, &asset_manager] {
    asset_manager.get_asset(ctx.device, "items-Sheet",
                            gatherer::AssetType::Texture);
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
  asset_manager.unload_assets(ctx.device);
  SDL_ReleaseWindowFromGPUDevice(ctx.device, ctx.window);
  SDL_DestroyWindow(ctx.window);
  SDL_DestroyGPUDevice(ctx.device);
  return EXIT_SUCCESS;
}
