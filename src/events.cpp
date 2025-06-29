#include <SDL3/SDL_log.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <expected>
#include <string>
#include <thread>

namespace gatherer {

enum class EventType : uint8_t { KeyPressedEvent, DamageEvent, Count };

struct EventHeader {
  EventType type;
  uint8_t size;
};

struct KeyPressedEvent {
  EventHeader header;
  int keycode;
  static constexpr EventType Type = EventType::KeyPressedEvent;

  KeyPressedEvent(int keycode) : keycode(keycode) {
    header.type = Type;
    header.size = static_cast<uint8_t>(sizeof(KeyPressedEvent));
  }
};

struct DamageEvent {
  EventHeader header;
  int entity;
  int amount;
  static constexpr EventType Type = EventType::DamageEvent;

  DamageEvent(int entity, int amount) : entity(entity), amount(amount) {
    header.type = Type;
    header.size = static_cast<uint8_t>(sizeof(DamageEvent));
  }
};

constexpr size_t MaxEventTypes = static_cast<size_t>(EventType::Count);
constexpr size_t MaxListeners = 8;
constexpr size_t MaxQueued = 64;
constexpr size_t MaxEventBytes = sizeof(DamageEvent);

static_assert(sizeof(KeyPressedEvent) <= MaxEventBytes,
              "Increase MaxEventBytes");
static_assert(sizeof(DamageEvent) <= MaxEventBytes, "Increase MaxEventBytes");

using EventFunc = void (*)(void *event, void *context);

struct Listener {
  EventType type;
  EventFunc fn;
  void *context;
};

class Dispatcher {
public:
  std::expected<void, std::string> subscribe(EventType type, EventFunc fn,
                                             void *context) {
    auto &slot = listener_count[static_cast<size_t>(type)];
    if (slot >= MaxListeners)
      return std::unexpected("MaxListeners exceeded!");
    listeners[static_cast<size_t>(type)][slot++] = {type, fn, context};
    return std::expected<void, std::string>{};
  }

  void dispatch(void *event) {
    auto header = static_cast<EventHeader *>(event);
    dispatch_to_listeners(header->type, event);
  }

  std::expected<void, std::string> queue_event(const void *event) {
    size_t current_tail;
    size_t next_tail;
    while (true) {
      current_tail = tail.load(std::memory_order_relaxed);
      next_tail = (tail + 1) % MaxQueued;

      if (next_tail == head.load(std::memory_order_acquire))
        return std::unexpected("MaxQueued events exceeded");
      auto dest = queue_buffer[current_tail];
      auto src = reinterpret_cast<const uint8_t *>(event);
      std::memcpy(dest, src, static_cast<size_t>(*(src + 1)));
      if (tail.compare_exchange_strong(current_tail, next_tail,
                                       std::memory_order_release)) {
        queued_count.fetch_add(1, std::memory_order_relaxed);
        return std::expected<void, std::string>{};
      }

      std::this_thread::yield();
    }
  }

  void update() {
    size_t current_head;
    size_t next_head;
    while (queued_count > 0) {
      current_head = head.load(std::memory_order_relaxed);
      next_head = (head + 1) % MaxQueued;
      auto slot = queue_buffer[current_head];
      auto header = reinterpret_cast<EventHeader *>(slot);
      dispatch_to_listeners(header->type, slot);
      head.store(next_head, std::memory_order_relaxed);
      queued_count.fetch_sub(1, std::memory_order_relaxed);
    }
  }

private:
  std::array<std::array<Listener, MaxListeners>, MaxEventTypes> listeners{};
  std::array<size_t, MaxEventTypes> listener_count{};

  //  alignas(alignof(max_align_t));
  uint8_t queue_buffer[MaxQueued][MaxEventBytes]{};
  std::atomic<std::size_t> head = 0;
  std::atomic<std::size_t> tail = 0;
  std::atomic<std::size_t> queued_count = 0;

  void dispatch_to_listeners(EventType type, void *event) {
    auto index = static_cast<size_t>(type);
    for (size_t i = 0, n = listener_count[index]; i < n; ++i) {
      listeners[index][i].fn(event, listeners[index][i].context);
    }
  }
};
} // namespace gatherer
