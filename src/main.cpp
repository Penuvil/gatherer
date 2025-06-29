#include <SDL3/SDL_init.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include "SDL3/SDL_main.h"
#include "toml.hpp"

#include "assets.cpp"
#include "async.cpp"
#include "events.cpp"
#include "gatherer.hpp"
#include <SDL3/SDL_gpu.h>

void on_input_event(void *raw, void *) {
  auto *event = reinterpret_cast<gatherer::KeyPressedEvent *>(raw);
}

void on_damage_event(void *raw, void *) {
  auto *event = reinterpret_cast<gatherer::DamageEvent *>(raw);
}

namespace gatherer {
enum class ErrorCode { SDLError };

struct ErrorInfo {
  ErrorCode code;
};

Task<void> input_system(Context *ctx) {
  DamageEvent damage = DamageEvent(5, 10);
  auto result = ctx->dispatcher->queue_event(&damage);
  if (!result.has_value()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", result.error().c_str());
  }
  KeyPressedEvent key_press = KeyPressedEvent(66);
  result = ctx->dispatcher->queue_event(&key_press);
  if (!result.has_value()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", result.error().c_str());
  }
  co_return;
}

Task<void> ai_system(Context *ctx) {
  (void)ctx;
  co_return;
}

Task<void> physics_system(Context *ctx) {
  auto result = co_await input_system(ctx);
  if (result.has_value()) {
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error: %s", result.error().c_str());
  }
  result = co_await ai_system(ctx);
  if (result.has_value()) {
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error: %s", result.error().c_str());
  }

  co_return;
}

Task<void> ui_system(Context *ctx) {
  auto result = co_await physics_system(ctx);
  if (result.has_value()) {
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error: %s", result.error().c_str());
  }
  co_return;
}

Task<void> game_update_system(Context *ctx) {
  auto result = co_await ui_system(ctx);
  if (result.has_value()) {
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error: %s", result.error().c_str());
  }
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
  ctx->dispatcher = new gatherer::Dispatcher;

  if (!SDL_ClaimWindowForGPUDevice(ctx->device, ctx->window)) {
    SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "Window: Width: %d, Height: %d\n",
              ctx->width, ctx->height);

  auto result = ctx->dispatcher->subscribe(gatherer::EventType::KeyPressedEvent,
                                           on_input_event, nullptr);
  if (!result.has_value()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", result.error().c_str());
  }
  result = ctx->dispatcher->subscribe(gatherer::EventType::DamageEvent,
                                      on_damage_event, nullptr);
  if (!result.has_value()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", result.error().c_str());
  }

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
