# ZMesh C++ Library

The C++ implementation of the ZMesh protocol mirrors the original .NET library that powers message box coordination over [ZeroMQ](https://zeromq.org/). It provides
low-level message boxes as well as a typed wrapper that integrates with modern C++ serializers.

## Features

* Connects to existing ZMesh message boxes using ZeroMQ `ROUTER`/`DEALER` sockets.
* Supports `tell`, `ask`, and `try_answer` workflows with automatic retry logic and answer caching.
* Provides a `TypedMessageBox` helper that works with arbitrary serializers. A ready-to-use `JsonSerializer` based on `nlohmann::json` is included.
* Ships with a CMake build that produces a static library.

> **Type names and interoperability**
>
> The typed API generates content-type strings from C++ types. By default the library uses compiler-provided type names (with demangling on GCC/Clang). If you need the names to match a specific schema (for example the PascalCase names produced by the .NET implementation), specialize `zmesh::type_name<T>::value()` for your types.

## Building

```bash
cmake -S zmesh-cpp -B build
cmake --build build
```

The build requires the following dependencies:

* [ZeroMQ](https://zeromq.org/) development headers and libraries.
* [`cppzmq`](https://github.com/zeromq/cppzmq) headers (`zmq.hpp`).
* [`nlohmann_json`](https://github.com/nlohmann/json) version 3.2.0 or later.

If CMake cannot automatically locate `zmq.hpp`, set `CPPZMQ_INCLUDE_DIR` to the folder that contains the header:

```bash
cmake -S zmesh-cpp -B build -DCPPZMQ_INCLUDE_DIR=/path/to/cppzmq
```

## Usage

```cpp
#include <zmesh/zmesh.hpp>
#include <zmesh/json_serializer.hpp>

int main()
{
    zmesh::ZMesh mesh{"5555", { {"backend", "localhost:6000"} }};
    auto serializer = zmesh::JsonSerializer{};
    auto backend = mesh.at("backend", serializer);

    backend->tell(std::string{"Hello"});

    auto answer = backend->ask<int, int>(42);
}
```

See the header files in `include/zmesh` for the complete API surface.

## Examples

The `examples` folder contains a minimal calculator client that connects to an
existing ZMesh router and demonstrates sending a typed `ask` request. Enable
the build flag when configuring CMake to compile the executable:

```bash
cmake -S zmesh-cpp -B build -DZMESH_BUILD_EXAMPLES=ON
cmake --build build --target zmesh_example_calculator
```

Run the resulting binary after starting a calculator service that listens on
`tcp://localhost:6000` and understands the `AddRequest`/`AddResponse` contract:

```bash
./build/examples/zmesh_example_calculator
```

