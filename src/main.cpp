#include "entt/signal/fwd.hpp"
#include <SDL3/SDL_init.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include "SDL3/SDL_main.h"
#include "entt/entt.hpp"
#include "toml.hpp"

#include "assets.cpp"
#include "async.cpp"
#include "gatherer.hpp"
#include <SDL3/SDL_gpu.h>

struct InputEvent {
  int key_code;
};

struct PhysicsEvent {
  float delta_time;
};

void on_input_event(const InputEvent &event) { (void)event; }

void on_physics_event(const PhysicsEvent &event) { (void)event; }

namespace gatherer {
enum class ErrorCode { SDLError };

struct ErrorInfo {
  ErrorCode code;
};

Task<void> input_system(Context *ctx) {
  printf("input Update Started\n");
  // co_await ResumeOnThreadPool{ctx->pool};
  // co_await std::suspend_never{};
  ctx->dispatcher->enqueue<InputEvent>(InputEvent{65});
  ctx->dispatcher->enqueue<InputEvent>(InputEvent{66});
  printf("input Update completed\n");
  co_return;
}

Task<void> ai_system(Context *ctx) {
  (void)ctx;
  // co_await ResumeOnThreadPool{ctx->pool};
  printf("AI Update Started\n");
  // co_await std::suspend_never{};
  printf("AI Update completed\n");
  co_return;
}

Task<void> physics_system(Context *ctx) {
  std::vector<Task<void>> tasks;
  tasks.push_back(input_system(ctx));
  tasks.push_back(ai_system(ctx));
  co_await input_system(ctx);
  co_await ai_system(ctx);
  // co_await wait_all(std::move(tasks));
  printf("Physics Update\n");

  co_return;
}

Task<void> ui_system(Context *ctx) {
  co_await physics_system(ctx);
  printf("UI Update\n");
  co_return;
}

Task<void> game_update_system(Context *ctx) {
  co_await ui_system(ctx);
  printf("Game Update\n");
  co_return;
}
} // namespace gatherer

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
              "Gatherer application initializing!\n");
  (void)argc;
  (void)argv;
  gatherer::Context *ctx = new gatherer::Context{};
  *appstate = static_cast<void *>(ctx);
  if (!SDL_Init(0)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s\n", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  auto config = toml::parse_file("resources/config.toml");
  ctx->width = config["window"]["width"].node()->as_integer()->get();
  ctx->height = config["window"]["height"].node()->as_integer()->get();
  ctx->window = SDL_CreateWindow("Gatherer", ctx->width, ctx->height, 0);
  if (ctx->window == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "%s\n", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  ctx->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
  if (ctx->device == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  ctx->asset_manager = new gatherer::AssetManager;
  ctx->pool = new gatherer::ThreadPool(4);
  ctx->dispatcher = new entt::dispatcher;

  if (!SDL_ClaimWindowForGPUDevice(ctx->device, ctx->window)) {
    SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "Window: Width: %d, Height: %d\n",
              ctx->width, ctx->height);

  ctx->dispatcher->sink<InputEvent>().connect<&on_input_event>();
  ctx->dispatcher->sink<PhysicsEvent>().connect<&on_physics_event>();

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  gatherer::Context *ctx = static_cast<gatherer::Context *>(appstate);
  (void)ctx;

  switch (event->type) {
  case SDL_EVENT_QUIT:
    return SDL_APP_SUCCESS;
  case SDL_EVENT_KEY_DOWN:
    switch (event->key.key) {
    case SDLK_Q:
      return SDL_APP_SUCCESS;
    default:
      break;
    }
    break;
  default:
    return SDL_APP_CONTINUE;
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  gatherer::Context *ctx = static_cast<gatherer::Context *>(appstate);

  auto task = gatherer::game_update_system(ctx);
  task.coro.resume();
  while (!task.coro.done()) {
    std::this_thread::yield();
  }

  ctx->dispatcher->update();

  std::this_thread::sleep_for(std::chrono::milliseconds(16));
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)result;
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
              "Gatherer application shutting down!\n");
  gatherer::Context *ctx = static_cast<gatherer::Context *>(appstate);

  ctx->asset_manager->unload_assets(ctx->device);
  delete (ctx->pool);
  delete (ctx->asset_manager);
  delete (ctx->dispatcher);
  SDL_ReleaseWindowFromGPUDevice(ctx->device, ctx->window);
  SDL_DestroyGPUDevice(ctx->device);
  SDL_DestroyWindow(ctx->window);
}
