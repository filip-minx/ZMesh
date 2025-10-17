# ZMesh
[![NuGet](https://img.shields.io/nuget/v/Minx.ZMesh.svg)](https://www.nuget.org/packages/Minx.ZMesh/)

## Overview

This repository contains the reference implementation of the ZMesh protocol. The
original distribution ships a .NET implementation. A modern C++ implementation
is now available so native applications can participate in the same messaging
topology.

## C++ Library

The `Minx.ZMesh.Cpp` project is a standards-conforming static library that
targets the latest C++ language level and uses [ZeroMQ](https://zeromq.org/)
with a lightweight binary framing layer for message transport.

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
   `cppzmq` and `zeromq` packages required by the library.

### Usage

The preferred entry point for native integrations is the message box workflow
that mirrors the .NET implementation. Payloads are plain strings, so
applications can encode data however they choose. The snippet below
demonstrates issuing a request through the `zmesh::ZMesh` router and handling
the string answer:

```cpp
#include <zmesh/request_options.hpp>
#include <zmesh/zmesh.hpp>

#include <chrono>
#include <iostream>
#include <optional>
#include <unordered_map>

int main() {
    std::unordered_map<std::string, std::string> system_map{{"Orders", "tcp://127.0.0.1:5555"}};

    zmesh::ZMesh mesh{std::nullopt, system_map};
    auto orders_box = mesh.at("Orders");

    const zmesh::RequestOptions options{
        std::chrono::seconds{1},
        1
    };

    const std::string content_type = "sample.order-status";
    const std::string payload = "OrderId=42;Action=Status";

    auto response = orders_box->ask(content_type, payload, options);

    std::cout << "Received answer (" << response.content_type << "): "
              << response.content << std::endl;
}
```

`zmesh::MessageBox` serialises messages with an internal binary framing format,
manages retries, and raises the same events available in the managed
implementation.

#### Hosting message boxes

`zmesh::ZMesh` mirrors the .NET class of the same name. It can optionally bind a
ZeroMQ router socket and surfaces message boxes backed by the same in-memory
queues and retry semantics as the managed version:

```cpp
#include <zmesh/message_box_processor.hpp>
#include <zmesh/zmesh.hpp>

#include <unordered_map>

int main() {
    std::unordered_map<std::string, std::string> system_map{{"Orders", "127.0.0.1:5555"}};

    zmesh::ZMesh mesh{"127.0.0.1:5555", system_map};
    auto orders_box = mesh.at("Orders");

    zmesh::MessageBoxProcessor processor{orders_box};
    processor.answer("sample.order-status", [](const std::string& request) {
        const bool is_status_query = request.find("Action=Status") != std::string::npos;
        const std::string status = is_status_query ? "Filled" : "Unknown";
        return zmesh::Answer{
            .content_type = "sample.order-status-response",
            .content = "Status=" + status};
    });

    for (;;) {
        processor.process_all();
    }
}
```

`zmesh::MessageBox` exposes the same `Tell`, `Ask`, `TryAnswer`, and
`TryListen` primitives as the managed `AbstractMessageBox`. Events
(`add_tell_received_handler` and `add_question_received_handler`) fire whenever
new payloads are queued by the router, enabling processors to react immediately
just like the C# `MessageBoxProcessor`.

Refer to the header files under `Minx.ZMesh.Cpp/include/zmesh` for the full set
of APIs and message model definitions.

### Sample console application

The solution also includes a `Minx.ZMesh.Cpp.Sample` console application that
demonstrates invoking the client library through message boxes. To run it, build
both the static library and the sample project, then launch the resulting
executable with an endpoint and message box name:

```powershell
Minx.ZMesh.Cpp.Sample.exe tcp://127.0.0.1:5555 Orders
```

The sample sends an `OrderStatus` request using plain string payloads and
prints the answer or any error returned by the client.
