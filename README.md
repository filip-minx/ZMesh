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

1. Install [vcpkg](https://github.com/microsoft/vcpkg) and integrate it with
   Visual Studio if you have not done so already.
2. Open the `Minx.ZMesh.sln` solution in Visual Studio 2022 or newer.
3. Ensure vcpkg is enabled for manifest mode (the project sets the required
   MSBuild properties automatically).
4. Build the `Minx.ZMesh.Cpp` project using the `x64` Debug or Release
   configurations. vcpkg will restore the `zeromq` and `nlohmann-json`
   dependencies declared in `Minx.ZMesh.Cpp/vcpkg.json`.

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
