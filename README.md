# ZMesh
[![NuGet](https://img.shields.io/nuget/v/Minx.ZMesh.svg)](https://www.nuget.org/packages/Minx.ZMesh/)

## C++ Support

The repository now includes a Visual Studio static library project located at `ZMesh.Cpp`. The library provides a modern C++23 implementation of the `IAbstractMessageBox` abstraction backed by ZeroMQ and nlohmann/json via vcpkg manifest dependencies. Open `Minx.ZMesh.sln` in Visual Studio 2022 (17.8 or newer) to build both the existing .NET projects and the new C++ library. Use `vcpkg` manifest mode to restore the `cppzmq` and `nlohmann-json` packages before building.
