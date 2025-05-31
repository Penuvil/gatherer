#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <variant>

#include "SDL3/SDL_gpu.h"
#include "SDL3_image/SDL_image.h"
namespace fs = std::filesystem;

constexpr std::string ASSETS = "resources/";

namespace gatherer {
uint32_t comupte_checkusm(const char *data, size_t length) {
  uint32_t checksum = 0;
  for (size_t i = 0; i < length; i++) {
    checksum += static_cast<uint8_t>(data[i]);
  }
  return checksum;
}

struct Texture {
  int width;
  int height;
  SDL_GPUTexture *handle;
};

enum class AssetType : uint8_t {
  Texture = 0,
};

using AssetVariant = std::variant<Texture>;

struct AssetHeader {
  AssetType type;
  uint8_t version;
  uint16_t reserved;
  uint32_t size;
};

class AssetManager {
public:
  AssetManager() : cache(std::unordered_map<std::string, AssetVariant>{}) {}
  ~AssetManager() {} // TODO unload assets

  void load_asset(SDL_GPUDevice *device, const std::string &name,
                  AssetType type) {
#ifdef DIST
    // TODO load from asset pack
#else
    switch (type) {
    case AssetType::Texture: {
      auto texture = Texture{};
      fs::path directory = ASSETS + "textures/";
      for (const auto &file : fs::directory_iterator(directory)) {
        auto file_stem = file.path().stem().string();
        if (file_stem == name) {
          std::cout << "Found File: " << file.path() << std::endl;
          auto image =
              IMG_Load(reinterpret_cast<const char *>(file.path().c_str()));
          if (image == NULL) {
            SDL_LogError(0, "Unable to load image %s",
                         reinterpret_cast<const char *>(file.path().c_str()));
          }

          texture.width = image->w;
          texture.height = image->h;

          const auto transfer_buffer_create_info =
              SDL_GPUTransferBufferCreateInfo{
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size = static_cast<Uint32>(image->w * image->h * 4)};
          auto texture_transfer_buffer =
              SDL_CreateGPUTransferBuffer(device, &transfer_buffer_create_info);
          auto texture_transfer_ptr =
              SDL_MapGPUTransferBuffer(device, texture_transfer_buffer, false);
          SDL_memcpy(texture_transfer_ptr, image->pixels,
                     image->w * image->h * 4);
          SDL_UnmapGPUTransferBuffer(device, texture_transfer_buffer);

          const auto texture_create_info = SDL_GPUTextureCreateInfo{
              .type = SDL_GPU_TEXTURETYPE_2D,
              .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
              .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
              .width = static_cast<Uint32>(image->w),
              .height = static_cast<Uint32>(image->h),
              .layer_count_or_depth = 1,
              .num_levels = 1};

          texture.handle = SDL_CreateGPUTexture(device, &texture_create_info);

          auto upload_cmd_buffer = SDL_AcquireGPUCommandBuffer(device);
          auto copy_pass = SDL_BeginGPUCopyPass(upload_cmd_buffer);

          auto texture_transfer_info = SDL_GPUTextureTransferInfo{
              .transfer_buffer = texture_transfer_buffer,
              .offset = 0,
          };

          auto texture_region =
              SDL_GPUTextureRegion{.texture = texture.handle,
                                   .w = static_cast<Uint32>(image->w),
                                   .h = static_cast<Uint32>(image->h),
                                   .d = 1};

          SDL_UploadToGPUTexture(copy_pass, &texture_transfer_info,
                                 &texture_region, false);

          SDL_EndGPUCopyPass(copy_pass);
          SDL_SubmitGPUCommandBuffer(upload_cmd_buffer);

          SDL_DestroySurface(image);
          SDL_ReleaseGPUTransferBuffer(device, texture_transfer_buffer);
          cache[name] = texture;
          return;
        }
      }
      std::cout << "No texture found with the name " << name << std::endl;
    }
    }
#endif
  }

  void unload_asset(SDL_GPUDevice *device, const std::string &name) {
    auto node = cache.extract(name);
    if (!node.empty()) {
      switch (node.mapped().index()) {
      case size_t(AssetType::Texture): {
        SDL_ReleaseGPUTexture(device, std::get<Texture>(node.mapped()).handle);
        break;
      }
      default:
        break;
      }
    }
  }

  void unload_assets(SDL_GPUDevice *device) {
    for (auto asset = cache.begin(); asset != cache.end();) {
      switch (asset->second.index()) {
      case static_cast<size_t>(AssetType::Texture): {
        SDL_ReleaseGPUTexture(device, std::get<Texture>(asset->second).handle);
        asset = cache.erase(asset);
        break;
      }
      default:
        break;
      }
    }
  }

  AssetVariant *get_asset(SDL_GPUDevice *device, const std::string &name,
                          AssetType type) {
    if (!cache.contains(name)) {
      load_asset(device, name, type);
    }
    return &cache[name];
  }

private:
  std::unordered_map<std::string, AssetVariant> cache;
};
} // namespace gatherer
