#ifndef REBUILD_CXX_HPP
#define REBUILD_CXX_HPP

namespace rebuild_cxx {

// Rebuilds from the command line recorded by '-frecord-command-line', then
// re-executes the program:
// - this library, if it is a shared library whose sources changed;
// - the running executable, if its sources changed.
// Returns if nothing needs rebuilding.
void if_needed(int argc, char **argv, char **envp);

} // namespace rebuild_cxx

#endif // REBUILD_CXX_HPP
