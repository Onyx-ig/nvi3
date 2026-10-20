# nvi3

A continuation of the classic `nvi` editor, with a built-in record-number database compatibility layer.

## Overview

`nvi3` is a Unix-style text editor project based on `nvi2`, exploring compatibility improvements for modern Linux systems without requiring the legacy Berkeley DB 1.85 library.

## Features

- Classic `vi` and `ex` editing interface.
- Built-in `dbshim` record-number emulation.
- CMake-based build system.
- Designed for experimentation and portability on Linux.

## Requirements

- C compiler
- CMake
- Make
- ncurses development files
- libbsd-overlay, where required by the platform

## Build

```sh
cmake -S . -B build -DCMAKE_C_FLAGS="-D_GNU_SOURCE"
cmake --build build -j4
```

Run the resulting editor:

```sh
./build/nvi
```

## Project Status

Experimental. The built-in record-number emulation is intended to replace the legacy Berkeley DB 1.85 dependency. Compatibility and recovery behavior should be tested across supported platforms.

## License

See the original project's license and copyright notices. This repository's license should be confirmed before redistribution.

## Informations

I created nvi3 on crux, having problem with nvi2 needing db1, so i created nvi3, to newer db, i edited almost everything, but it was worth it, finally im gonna stop using nvim
