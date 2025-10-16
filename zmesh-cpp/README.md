# ZMesh C++ Library

The C++ implementation of the ZMesh protocol mirrors the original .NET library that powers message box coordination over [ZeroMQ](https://zeromq.org/). It provides
low-level message boxes as well as a typed wrapper that integrates with modern C++ serializers.

## Features

* Connects to existing ZMesh message boxes using ZeroMQ `ROUTER`/`DEALER` sockets.
* Supports `tell`, `ask`, and `try_answer` workflows with automatic retry logic and answer caching.
* Provides a `TypedMessageBox` helper that works with arbitrary serializers. A ready-to-use `JsonSerializer` based on `nlohmann::json` is included.
* Ships with a Visual Studio solution that produces a static library and optional examples.

> **Type names and interoperability**
>
> The typed API generates content-type strings from C++ types. By default the library uses compiler-provided type names (with demangling on GCC/Clang). If you need the names to match a specific schema (for example the PascalCase names produced by the .NET implementation), specialize `zmesh::type_name<T>::value()` for your types.

## Building

The repository includes a Visual Studio 2022 solution file: [`ZMeshCpp.sln`](./ZMeshCpp.sln). The projects assume the following third-party dependencies are available:

* [ZeroMQ](https://zeromq.org/) development headers and libraries. The repository now vendors the full [libzmq 4.3.5](https://github.com/zeromq/libzmq) source tree under `deps/zeromq/libzmq`, and the Visual Studio projects compile it into the `zmesh-cpp` static library automatically. The upstream MPL 2.0 license remains available in `deps/zeromq/LICENSE.libzmq`.
* [`cppzmq`](https://github.com/zeromq/cppzmq) headers (`zmq.hpp`). The repository vendors the official single-header distribution (currently v4.10.0) under `deps/zeromq/include` together with its MIT license in `deps/zeromq/LICENSE`. Replace it with a newer release if you require additional features.
* [`nlohmann_json`](https://github.com/nlohmann/json) headers. A minimal, header-only compatibility shim is bundled under `deps/nlohmann_json`, so the projects build out of the box. You can replace it with the official single-header release if you rely on additional JSON features.

By default the projects expect these dependencies to live under `zmesh-cpp/deps` with the structure below:

```
zmesh-cpp/
  deps/
    zeromq/
      include/zmq.h
      include/zmq.hpp
      libzmq/include/...
      libzmq/src/...
    nlohmann_json/
      include/nlohmann/json.hpp
```

If your environment uses a different layout, open the project properties, navigate to **Configuration Properties → VC++ Directories**, and update the `ZeroMQIncludeDir` and `NlohmannJsonIncludeDir` user macros. Visual Studio will then discover the correct headers and libraries.

To build the static library and the calculator example:

1. Open `ZMeshCpp.sln` in Visual Studio 2022.
2. Choose the desired configuration (Debug or Release) and the x64 platform.
3. Build either the `zmesh-cpp` static library project or the `calculator-example` application.

The projects link against the required Windows system libraries (`Ws2_32`, `Rpcrt4`, `Iphlpapi`, and `Advapi32`) so you do not need to configure them manually when using the bundled `libzmq` sources.

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

The `examples` folder contains a minimal calculator client that connects to an existing ZMesh router and demonstrates sending a typed `ask` request. Build the `calculator-example` project inside Visual Studio after configuring the dependencies above.

Run the resulting binary after starting a calculator service that listens on `tcp://localhost:6000` and understands the `AddRequest`/`AddResponse` contract.
