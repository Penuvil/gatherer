#include <stdio.h>

#include "toml++/toml.hpp"

struct Context {
  int width;
  int height;
};

void init(Context& ctx) {
  auto config = toml::parse_file("resources/config.toml");
  ctx.width = config["window"]["width"].node()->as_integer()->get();
  ctx.height = config["window"]["height"].node()->as_integer()->get();
}

int main(int argc, char* argv[]) {
  Context ctx = {};
  init(ctx);
  printf("Hello world!\n");
  printf("Window: Width: %d, Height: %d\n", ctx.width, ctx.height);
}
