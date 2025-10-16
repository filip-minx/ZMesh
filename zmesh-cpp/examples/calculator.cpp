#include <zmesh/json_serializer.hpp>
#include <zmesh/zmesh.hpp>

#include <chrono>
#include <iostream>
#include <optional>
#include <nlohmann/json.hpp>

namespace
{
    struct AddRequest
    {
        int left{};
        int right{};
    };

    struct AddResponse
    {
        int sum{};
    };

    void to_json(nlohmann::json& json, const AddRequest& request)
    {
        json = nlohmann::json{{"left", request.left}, {"right", request.right}};
    }

    void from_json(const nlohmann::json& json, AddRequest& request)
    {
        json.at("left").get_to(request.left);
        json.at("right").get_to(request.right);
    }

    void to_json(nlohmann::json& json, const AddResponse& response)
    {
        json = nlohmann::json{{"sum", response.sum}};
    }

    void from_json(const nlohmann::json& json, AddResponse& response)
    {
        json.at("sum").get_to(response.sum);
    }
}

int main()
{
    // Configure a mesh that knows about a calculator service.
    // The service is expected to be reachable at tcp://localhost:6000.
    zmesh::ZMesh mesh{std::nullopt, {{"calculator", "localhost:6000"}}};

    auto serializer = zmesh::JsonSerializer{};
    auto calculator = mesh.at("calculator", serializer);

    // Ask the service to add two numbers. The request and response payloads
    // are automatically serialized via nlohmann::json.
    const AddRequest request{21, 21};

    try
    {
        auto response = calculator->ask<AddRequest, AddResponse>(request, std::chrono::seconds{5});
        std::cout << request.left << " + " << request.right << " = " << response.sum << '\n';
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Failed to receive response: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
