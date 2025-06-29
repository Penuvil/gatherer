#ifndef GATHERER_H_
#define GATHERER_H_

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

namespace gatherer {
class AssetManager;
struct ThreadPool;
class Dispatcher;

struct Context {
  AssetManager *asset_manager;
  ThreadPool *pool;
  gatherer::Dispatcher *dispatcher;
  SDL_Window *window;
  SDL_GPUDevice *device;
  int width;
  int height;
};
} // namespace gatherer

#endif // GATHERER_H_
