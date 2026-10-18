//
// Builds librebuild_cxx.so and librebuild_cxx_auto.so into out/, and
// rebuilds itself first when build.cpp or rebuild_cxx.cpp change.
//
// Bootstrap once with ./bootstrap.sh, then just run ./build.
//

#include "rebuild_cxx.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <print>
#include <string>

namespace fs = std::filesystem;

namespace {

void sh(const std::string &cmd) {
  std::println(stderr, "+ {}", cmd);
  if (std::system(cmd.c_str()) != 0)
    std::exit(1);
}

} // namespace

int main(int argc, char **argv, char **envp) {
  rebuild_cxx::if_needed(argc, argv, envp);

  fs::create_directories("out");

  // The libraries record their command line too, so they can rebuild
  // themselves. Use absolute paths, so it works from whatever directory the
  // program loading them runs in.
  const std::string dir = fs::current_path().string();
  const std::string cxx = "clang++ -std=c++23 -O2 -Wall -Wextra -fPIC -shared "
                          "-frecord-command-line";

  sh(std::format("{0} '{1}/rebuild_cxx.cpp' -o '{1}/out/librebuild_cxx.so'",
                 cxx, dir));
  sh(std::format("{0} '{1}/rebuild_cxx.cpp' '{1}/rebuild_cxx_auto.cpp' "
                 "-o '{1}/out/librebuild_cxx_auto.so'",
                 cxx, dir));

  std::println("\nTo rebuild any program compiled with -frecord-command-line "
               "without relinking it:\n  LD_PRELOAD={}/out/librebuild_cxx_auto.so"
               " <program>",
               dir);
}
