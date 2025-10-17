#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

#include <zmesh/request_options.hpp>
#include <zmesh/zmesh.hpp>

namespace {

void print_usage(const char* executable_name) {
    std::cout << "Usage: " << executable_name << " <endpoint> <message-box>" << std::endl;
    std::cout << "\nExample:" << std::endl;
    std::cout << "  " << executable_name << " tcp://127.0.0.1:5555 Orders" << std::endl;
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

    try {
        std::unordered_map<std::string, std::string> system_map{{message_box, endpoint}};
        zmesh::ZMesh mesh{std::nullopt, std::move(system_map)};
        auto box = mesh.at(message_box);

        const zmesh::RequestOptions options{
            std::chrono::milliseconds{1000},
            1
        };

        std::cout << "Sending OrderStatus request via message box..." << std::endl;
        const std::string content_type = "sample.order-status";
        const std::string payload = "OrderId=42;Action=Status";
        const auto response = box->ask(content_type, payload, options);
        std::cout << "Received answer of type " << response.content_type << ": " << response.content << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Request failed: " << ex.what() << std::endl;
        std::cerr << "Ensure a ZMesh broker is reachable at the given endpoint." << std::endl;
        return 2;
    }

    return 0;
}
