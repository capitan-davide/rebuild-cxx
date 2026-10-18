#include "rebuild_cxx.hpp"

namespace {

// Runs rebuild_cxx::if_needed() before main and before any static constructor
// of the program, so a stale binary never runs its own code.
//
// Priority 101 is the earliest available to user code (0-100 are reserved),
// and constructors with a priority runs before those without one, which
// includes all ortinary static constructors.
//
// glibc passes (argc, argv, envp) to constructor functions.
[[gnu::constructor(101)]]
void auto_rebuild(int argc, char **argv, char **envp) {
  rebuild_cxx::if_needed(argc, argv, envp);
};

} // namespace
