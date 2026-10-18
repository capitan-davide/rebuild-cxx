#include "rebuild_cxx.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <dlfcn.h>
#include <link.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::error_code last_error() noexcept {
  return {errno, std::system_category()};
}

std::expected<fs::path, std::error_code> get_exe_path() {
  std::error_code ec;
  fs::path exe_path = fs::read_symlink("/proc/self/exe", ec);
  if (ec)
    return std::unexpected{ec};

  // The binary was deleted or replaced while running.
  if (exe_path.native().ends_with(" (deleted)"))
    return std::unexpected{
        std::make_error_code(std::errc::no_such_file_or_directory)};

  return exe_path;
}

// Returns the first command line recorded by -frecord-command-line in the ELF
// file, or an empty string if there is none.
std::string compiler_options(const fs::path &file) {
  std::ifstream f{file, std::ios::binary};
  std::string elf{std::istreambuf_iterator<char>(f), {}};
  if (elf.size() < sizeof(ElfW(Ehdr)))
    return {};

  // The ELF header sits at offset 0 and tells us where the section header
  // table is (e_shoff) and how many entries it has (e_shnum).
  auto *eh = reinterpret_cast<const ElfW(Ehdr) *>(elf.data());
  auto *sh = reinterpret_cast<const ElfW(Shdr) *>(elf.data() + eh->e_shoff);

  // Section names are not stored in the headers themselves: each sh_name is an
  // offset into a string table section, whose index is e_shstrndx.
  const char *names = elf.data() + sh[eh->e_shstrndx].sh_offset;

  for (ElfW(Half) i{0}; i < eh->e_shnum; ++i) {
    if (std::string_view{names + sh[i].sh_name} != ".GCC.command.line")
      continue;

    std::string_view opts{elf.data() + sh[i].sh_offset, sh[i].sh_size};
    return std::string{opts.substr(0, opts.find('\0'))};
  }

  return {};
}

// Splits a command line recorded by -frecord-command-line into arguments, in
// place: arguments are separated by spaces, and a space or backslash inside an
// argument is escaped with '\'. The returned pointers point into cmdline.
std::vector<char *> split_args(std::string &cmdline) {
  auto out = cmdline.begin();
  for (auto in = cmdline.begin(); in != cmdline.end(); ++in) {
    if (*in == '\\' && std::next(in) != cmdline.end())
      *out++ = *++in;
    else if (*in == ' ')
      *out++ = '\0';
    else
      *out++ = *in;
  }
  cmdline.erase(out, cmdline.end());

  std::vector<char *> args;
  for (auto arg : cmdline | std::views::split('\0'))
    if (!arg.empty())
      args.push_back(arg.data());
  return args;
}

bool is_source(std::string_view arg) {
  for (std::string_view ext : {".c", ".cc", ".cpp", ".cxx", ".c++", ".C"})
    if (arg.ends_with(ext))
      return true;
  return false;
}

// True if any source in args is newer than file. Sources that do not exist
// (e.g. when running from another directory) are ignored, so a binary whose
// sources are not around never rebuilds.
bool needs_rebuild(std::span<const char *const> args, const fs::path &file) {
  std::error_code ec;
  auto file_time = fs::last_write_time(file, ec);
  if (ec)
    return false;

  auto srcs = args | std::views::drop(1) | std::views::filter(is_source);
  return std::ranges::any_of(srcs, [&](const char *src) {
    auto src_time = fs::last_write_time(src, ec);
    return !ec && src_time > file_time;
  });
}

// Set only in the environment of the compiler we run, never in our own, so the
// re-executed program still checks itself.
//
// LD_PRELOAD is inherited, so when this library was preloaded the compiler
// loads it too and calls if_needed(). The library is still stale (we are the
// ones rebuilding it), so without this guard the compiler would spawn another
// compiler to rebuild it, which would do the same, forever.
constexpr const char *active_var = "REBUILD_CXX_ACTIVE";

// Runs args (looked up in PATH) with envp plus active_var, and waits for it;
// returns its exit status.
std::expected<int, std::error_code> run(std::span<char *> args, char **envp) {
  std::vector<char *> env;
  for (char **e = envp; *e; ++e)
    env.push_back(*e);
  std::string active = std::string{active_var} + "=1";
  env.push_back(active.data());
  env.push_back(nullptr);

  pid_t pid;
  if (int err = ::posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(),
                               env.data()))
    return std::unexpected{std::error_code{err, std::system_category()}};

  int status;
  if (::waitpid(pid, &status, 0) < 0)
    return std::unexpected{last_error()};
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

// Re-runs the command line recorded in file if its sources changed; returns
// whether it did. Exits if the rebuild fails.
bool rebuild_if_stale(const fs::path &file, char **envp) {
  std::string opts = compiler_options(file);
  std::vector<char *> args = split_args(opts);
  if (args.empty() || !needs_rebuild(args, file))
    return false;
  args.push_back(nullptr);

  std::println(stderr, "rebuilding {}", file.string());
  auto rc = run(args, envp);
  if (!rc) {
    std::println(stderr, "rebuild failed: {}", rc.error().message());
    std::exit(1);
  }
  if (*rc != 0) {
    std::println(stderr, "rebuild failed: compiler exited with {}", *rc);
    std::exit(1);
  }
  return true;
}

[[noreturn]] void exec(const fs::path &exe_path, char **argv, char **envp) {
  std::fflush(stdout);
  ::execve(exe_path.c_str(), argv, envp);
  std::println(stderr, "exec failed: {}", last_error().message());
  std::exit(1);
}

// The shared library containing this code, or an empty path if it is linked
// into the executable itself.
fs::path library_path(const fs::path &exe_path) {
  Dl_info info;
  if (!::dladdr(reinterpret_cast<void *>(&rebuild_if_stale), &info) ||
      !info.dli_fname)
    return {};

  std::error_code ec;
  fs::path lib = info.dli_fname;
  if (fs::equivalent(lib, exe_path, ec) || ec)
    return {};
  return lib;
}

} // namespace

namespace rebuild_cxx {

void if_needed(int /*argc*/, char **argv, char **envp) {
  // We are (a child of) the compiler run by a rebuild: see active_var.
  if (std::getenv(active_var))
    return;

  // Get the path before rebuilding: once the binary is replaced,
  // /proc/self/exe ends with " (deleted)" and get_exe_path() fails.
  auto exe_path = get_exe_path();
  if (!exe_path)
    return;

  // Rebuild this library first and restart, so the new process loads it and
  // uses the new code to check the executable.
  if (fs::path lib = library_path(*exe_path);
      !lib.empty() && rebuild_if_stale(lib, envp))
    exec(*exe_path, argv, envp);

  if (rebuild_if_stale(*exe_path, envp))
    exec(*exe_path, argv, envp);
}

} // namespace rebuild_cxx
