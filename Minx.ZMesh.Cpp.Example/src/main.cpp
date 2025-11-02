#include "Minx/ZMesh/AbstractMessageBox.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main()
{
    const std::string endpoint{"tcp://127.0.0.1:5566"};

    auto responder = std::make_shared<Minx::ZMesh::AbstractMessageBox>(
        endpoint,
        Minx::ZMesh::AbstractMessageBox::ConnectionMode::Bind);
    auto caller = std::make_shared<Minx::ZMesh::AbstractMessageBox>(
        endpoint,
        Minx::ZMesh::AbstractMessageBox::ConnectionMode::Connect);

    responder->OnTellReceived([](const Minx::ZMesh::MessageReceivedEventArgs& args) {
        std::cout << "[responder] tell received (type=" << args.ContentType() << ")" << std::endl;
    });

    responder->TryListen("example/tell", [](const std::string& payload) {
        std::cout << "[responder] tell payload: " << payload << std::endl;
    });

    responder->TryAnswer("example/question", [](const std::string& payload) {
        std::cout << "[responder] question payload: " << payload << std::endl;
        return Minx::ZMesh::Response{
            .content_type = "example/answer",
            .content = std::string{"replying to: "} + payload,
        };
    });

    caller->Tell("example/tell", "hello from the caller!");
    const auto answer = caller->Ask("example/question", "how are you?");

    std::cout << "[caller] received answer type='" << answer.content_type
              << "' content='" << answer.content << "'" << std::endl;

    std::this_thread::sleep_for(200ms);

    return 0;
}
