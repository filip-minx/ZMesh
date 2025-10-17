# ZMesh
[![NuGet](https://img.shields.io/nuget/v/Minx.ZMesh.svg)](https://www.nuget.org/packages/Minx.ZMesh/)

## Native C++ library

The solution now ships with a Visual Studio static library project named `Minx.ZMesh.Native`. It implements the `IAbstractMessageBox` contract in modern C++ using ZeroMQ for transport. The project is configured for `x64` builds and consumes its dependencies (`cppzmq` and `zeromq`) through the included `vcpkg.json` manifest. Visual Studio 2022 automatically restores these packages when the solution is opened with vcpkg integration enabled.

The library exposes a `zmesh::ZmqMessageBox` type that matches the core messaging capabilities of the managed implementation, including publish/subscribe tells, asynchronous questions, pending question retrieval, cancellation, and timeout handling.
