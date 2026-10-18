# rebuild-cxx

Makes a C++ program recompile and restart itself when its sources change, by
re-running the command line Clang recorded in the binary. The libraries do the
same for themselves: when started, a program first rebuilds a stale library,
then itself.

Requires Linux, glibc and clang++.

## Build

```sh
./bootstrap.sh   # first time; afterwards just ./build
```

## Use

Compile your program with `-frecord-command-line`, in a single clang++
invocation (`R` is this repository), and either:

- call it explicitly at the start of `main`:

  ```cpp
  #include "rebuild_cxx.hpp"

  int main(int argc, char **argv, char **envp) {
    rebuild_cxx::if_needed(argc, argv, envp);
    // ...
  }
  ```

  ```sh
  clang++ -std=c++23 -frecord-command-line -I$R main.cpp -L$R/out -lrebuild_cxx -Wl,-rpath,$R/out
  ```

- or link the auto library, which runs it before `main`:

  ```sh
  clang++ -std=c++23 -frecord-command-line main.cpp -L$R/out -Wl,--no-as-needed -lrebuild_cxx_auto -Wl,-rpath,$R/out
  ```

- or preload it, without relinking:

  ```sh
  LD_PRELOAD=$R/out/librebuild_cxx_auto.so ./a.out
  ```

Run the program from the directory it was compiled in. Only `.cpp`/`.c` files
are checked: changing a header alone does not trigger a rebuild.

Inspired by [nob.h](https://github.com/tsoding/nob.h).
