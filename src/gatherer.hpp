#ifndef GATHERER_H_
#define GATHERER_H_

#include "SDL3/SDL.h"
#include "entt/entt.hpp"

namespace gatherer {
class AssetManager;
struct ThreadPool;

struct Context {
  AssetManager *asset_manager;
  ThreadPool *pool;
  entt::dispatcher *dispatcher;
  SDL_Window *window;
  SDL_GPUDevice *device;
  int width;
  int height;
};
} // namespace gatherer

#endif // GATHERER_H_
