# ZMesh
[![NuGet](https://img.shields.io/nuget/v/Minx.ZMesh.svg)](https://www.nuget.org/packages/Minx.ZMesh/)

## Overview

This repository contains the reference implementation of the ZMesh protocol. The
original distribution ships a .NET implementation. A modern C++ implementation
is now available so native applications can participate in the same messaging
topology.

## C++ Library

The `Minx.ZMesh.Cpp` project is a standards-conforming static library that
targets the latest C++ language level and uses [ZeroMQ](https://zeromq.org/) and
[nlohmann/json](https://github.com/nlohmann/json) for transport and
serialization respectively.

### Building with Visual Studio

1. Install [vcpkg](https://github.com/microsoft/vcpkg) if it is not already
   available on your machine:

   ```powershell
   git clone https://github.com/microsoft/vcpkg.git
   .\vcpkg\bootstrap-vcpkg.bat
   ```

2. Integrate vcpkg with MSBuild once so that Visual Studio can automatically
   restore manifest dependencies:

   ```powershell
   .\vcpkg\vcpkg.exe integrate install
   ```

   (If vcpkg lives somewhere other than your repository root, set the
   `VCPKG_ROOT` environment variable to its location.)

3. Open the `Minx.ZMesh.sln` solution in Visual Studio 2022 or newer and select
   either the `Debug|x64` or `Release|x64` configuration.

4. Build the `Minx.ZMesh.Cpp` project. The MSBuild integration restores the
   manifest found at `Minx.ZMesh.Cpp/vcpkg.json`, which supplies the
   `cppzmq`, `zeromq`, and `nlohmann-json` packages required by the library.

### Usage

The entry point for native integrations is the `zmesh::ZMeshClient` class in the
`include/zmesh` directory. A minimal usage example is shown below:

```cpp
#include <zmesh/zmesh_client.hpp>
#include <nlohmann/json.hpp>

int main() {
    zmesh::ZMeshClient client{"127.0.0.1:5555", "Orders"};

    nlohmann::json payload = {
        {"OrderId", 42},
        {"Action", "Status"}
    };

    auto answer = client.ask("OrderStatus", payload);
    std::cout << "Received answer: " << answer.content << std::endl;
}
```

`ZMeshClient` provides templated helpers to work directly with types that have
`nlohmann::json` conversions. The implementation also retries requests and
propagates detailed failures using C++ exceptions.

Refer to the header files under `Minx.ZMesh.Cpp/include/zmesh` for the full set
of APIs and message model definitions.
