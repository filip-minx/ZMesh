#include <chrono>
#include <iostream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <zmesh/zmesh_client.hpp>

namespace {

void print_usage(const char* executable_name) {
    std::cout << "Usage: " << executable_name << " <endpoint> <message-box> [identity]" << std::endl;
    std::cout << "\nExample:" << std::endl;
    std::cout << "  " << executable_name << " tcp://127.0.0.1:5555 Orders sample-client" << std::endl;
    std::cout << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string endpoint = argv[1];
    const std::string message_box = argv[2];
    const std::optional<std::string> identity = argc >= 4 ? std::optional<std::string>{argv[3]} : std::nullopt;

    try {
        zmesh::ZMeshClient client{endpoint, message_box, identity};

        const nlohmann::json payload = {
            {"OrderId", 42},
            {"Action", "Status"}
        };

        const zmesh::RequestOptions options{
            std::chrono::milliseconds{1000},
            1
        };

        std::cout << "Sending OrderStatus request..." << std::endl;
        const auto answer = client.ask("OrderStatus", payload, options);
        std::cout << "Received answer with status " << answer.status_code << ': ' << answer.content << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Request failed: " << ex.what() << std::endl;
        std::cerr << "Ensure a ZMesh broker is reachable at the given endpoint." << std::endl;
        return 2;
    }

    return 0;
}
