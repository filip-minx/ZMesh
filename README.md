# ZMesh

[![NuGet](https://img.shields.io/nuget/v/Minx.ZMesh.svg)](https://www.nuget.org/packages/Minx.ZMesh/)

ZMesh is a lightweight message bus that uses ZeroMQ/NetMQ to expose distributed "message boxes" that can exchange typed
messages across languages and processes. It provides a high-level API for issuing questions, sending one-way tells, and
replying with answers while the underlying router handles routing, retries, and back-pressure.

## Contents

- [Concepts](#concepts)
- [Protocol Format](#protocol-format)
  - [Dealer ➜ Router Frames](#dealer--router-frames)
  - [Router ➜ Dealer Frames](#router--dealer-frames)
- [Supported Languages](#supported-languages)
- [Implementing a New Language Binding](#implementing-a-new-language-binding)
  - [Required Primitives](#required-primitives)
  - [Best Practices](#best-practices)

## Concepts

ZMesh revolves around a few core abstractions that are shared across implementations:

- **Message box** – A named endpoint backed by a ZeroMQ DEALER socket. Each message box connects to a central router and
  owns the queues for tells, pending questions, and awaiting answers. The .NET implementation encapsulates this logic in
  `AbstractMessageBox` and `TypedMessageBox` for strongly typed models.【F:Minx.ZMesh/AbstractMessageBox.cs†L14-L205】【F:Minx.ZMesh/TypedMessageBox.cs†L9-L109】
- **Tell** – A one-way message delivered to listeners on the target box. Tells are enqueued per content type and surfaced
  through `TryListen`/`TryListenGeneric` helpers.【F:Minx.ZMesh/AbstractMessageBox.cs†L110-L154】
- **Question** – A request message that expects an answer. Questions carry a unique correlation identifier and are stored
  until an answering worker picks them up via `TryAnswer`/`GetQuestion` or their typed equivalents.【F:Minx.ZMesh/Models/QuestionMessage.cs†L1-L10】【F:Minx.ZMesh/AbstractMessageBox.cs†L156-L230】
- **Answer** – The response to a question. Answers reuse the correlation identifier so that the asking side can match the
  response with its pending task completion source.【F:Minx.ZMesh/Models/AnswerMessage.cs†L1-L8】【F:Minx.ZMesh/AbstractMessageBox.cs†L232-L279】
- **System map** – A dictionary that maps message box names to network addresses. It allows the router to lazily create
  message box connections for every system defined in the map.【F:Minx.ZMesh/ZMesh.cs†L16-L41】【F:Minx.ZMesh/ZMesh.cs†L68-L86】
- **Serializer** – Pluggable component used to transform typed payloads to strings. The default serializer is JSON, but
  any serializer implementing `ISerializer` can be swapped in.【F:Minx.ZMesh/TypedMessageBox.cs†L1-L70】

## Protocol Format

ZMesh uses a ROUTER/DEALER pattern over TCP. Every logical message is split into frames, and both sides must agree on the
frame ordering. All payloads are UTF-8 encoded strings unless noted otherwise.

### Dealer ➜ Router Frames

When a message box talks to the router, it sends one of two message types:

| Frame Index | Tell Message                                | Question Message                                   |
|-------------|----------------------------------------------|----------------------------------------------------|
| 0           | Message box name                             | Message box name                                   |
| 1           | Literal string `"Tell"`                      | Literal string `"Question"`                        |
| 2           | Empty string                                 | Correlation identifier (GUID recommended)          |
| 3           | Payload content type (usually CLR type name) | Payload content type                               |
| 4           | Payload content                              | Payload content                                    |

This format is enforced by the .NET message box when dequeuing queued messages for transmission.【F:Minx.ZMesh/AbstractMessageBox.cs†L60-L110】

### Router ➜ Dealer Frames

Responses and server-pushed tells originate at the router. The native implementation shows the exact frame sequence sent
back to the DEALER identity:

| Frame Index | Description                                                                |
|-------------|----------------------------------------------------------------------------|
| 0           | Dealer identity supplied by ZeroMQ/NetMQ (not visible to the application) |
| 1           | Literal string `"Answer"`                                                  |
| 2           | Message box name                                                           |
| 3           | Correlation identifier                                                     |
| 4           | Answer content type                                                        |
| 5           | Answer payload                                                             |

The router reads incoming frames in the order shown above, determines whether the message is a tell or question, and
either dispatches it to a message box queue or enqueues a corresponding answer back onto the socket.【F:Minx.ZMesh/ZMesh.cs†L44-L158】【F:Minx.ZMesh/ZMesh.cs†L23-L43】
The C++ implementation mirrors the same structure when receiving frames from ZeroMQ.【F:Minx.ZMesh.Native/src/zmesh.cpp†L58-L132】

## Supported Languages

The repository currently ships with two production-ready bindings:

- **.NET / C#** – Located in `Minx.ZMesh` with optional helper projects under `Minx.ZMesh.Example` and
  `Minx.ZMesh.Probe`. This package is published on NuGet as `Minx.ZMesh` and targets the NetMQ transport layer.【F:Minx.ZMesh/AbstractMessageBox.cs†L14-L279】【F:Minx.ZMesh/TypedMessageBox.cs†L9-L156】
- **C++17** – Provided in `Minx.ZMesh.Native`, using the ZeroMQ C++ bindings (`zmq.hpp`). The header-only API mirrors the
  .NET abstractions, allowing native services to participate in the mesh via the same protocol.【F:Minx.ZMesh.Native/include/minx/zmesh/types.hpp†L1-L65】【F:Minx.ZMesh.Native/src/zmesh.cpp†L1-L168】

## Implementing a New Language Binding

The protocol is intentionally small so additional languages can participate. A compliant implementation needs only a few
primitives and design conventions.

### Required Primitives

To interoperate you must implement the following building blocks:

1. **Router host** – A ROUTER socket that binds to `tcp://<host>:<port>`, maps message box names to their remote DEALER
   addresses, and forwards answers. The router must buffer questions and tells and forward answers using the frame layout
   described above.【F:Minx.ZMesh/ZMesh.cs†L16-L116】【F:Minx.ZMesh/ZMesh.cs†L117-L158】
2. **Message box client** – A DEALER socket per message box name that connects to the router and manages local queues of
   tells, pending questions, and awaiting answers. It is responsible for retrying unanswered questions and caching
   correlation IDs to deduplicate retries.【F:Minx.ZMesh/AbstractMessageBox.cs†L14-L279】
3. **Serialization hooks** – A pluggable serializer that can produce a content type identifier and UTF-8 payload string
   for each message. This can be JSON, Protobuf, or any other format as long as both sides agree on the `content_type`
   string.【F:Minx.ZMesh/TypedMessageBox.cs†L9-L156】
4. **Pending question queue** – Storage for inbound questions, preserving the original dealer identity so the answer can
   be routed back when ready.【F:Minx.ZMesh/AbstractMessageBox.cs†L156-L228】【F:Minx.ZMesh.Native/include/minx/zmesh/types.hpp†L42-L65】

### Best Practices

- **Normalize frame handling** – Always trim trailing null characters on frames received from ZeroMQ to avoid content type
  mismatches. The C++ router helper does this before dispatching messages.【F:Minx.ZMesh.Native/src/zmesh.cpp†L24-L57】
- **Handle retries and cancellations** – Implement a bounded retry policy for unanswered questions and ensure cancellation
  tokens (or equivalent) remove pending correlation IDs so that duplicate answers are ignored.【F:Minx.ZMesh/AbstractMessageBox.cs†L200-L279】
- **Cache answers for duplicates** – Optional but recommended: when a duplicate question arrives with the same correlation
  ID, respond immediately using a short-lived cache. The .NET message box caches answers for one minute to satisfy
  late-arriving retries.【F:Minx.ZMesh/AbstractMessageBox.cs†L180-L228】
- **Surface typed APIs** – Wrap the raw string protocol in typed helpers so application code can work with domain models
  instead of manual serialization calls. `TypedMessageBox` is a good blueprint for higher-level language bindings.【F:Minx.ZMesh/TypedMessageBox.cs†L9-L156】
- **Keep system maps authoritative** – Validate that every requested message box exists in the system map and fail fast if
  it is missing to avoid silent drops.【F:Minx.ZMesh/ZMesh.cs†L68-L86】

With these pieces in place, any language with ZeroMQ bindings can exchange messages with existing ZMesh services while
retaining native ergonomics.
