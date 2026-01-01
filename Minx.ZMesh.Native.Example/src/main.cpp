#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <unordered_map>

#include "minx/zmesh/zmesh.hpp"

int main() {
    using namespace std::chrono_literals;

    std::unordered_map<std::string, std::string> system_map{
        {"BoxA", "127.0.0.1:7000"},
        {"BoxB", "127.0.0.1:7001"},
    };

    minx::zmesh::ZMesh node_a{"127.0.0.1:7000", system_map};
    minx::zmesh::ZMesh node_b{"127.0.0.1:7001", system_map};

    std::jthread worker_a([&](std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            auto box_a = node_a.At("BoxA");
            auto box_b = node_a.At("BoxB");

            box_a->TryListen("HelloMsg", [](const std::string& content) {
                std::cout << "BoxA received Hello with content: " << content << '\n';
            });

            box_b->Tell("HelloMsg", "Greetings from node A");

            auto answer_future = box_b->Ask("WhatIsYourName", "Node A is asking");
            try {
                auto answer = answer_future.get();
                std::cout << "Node A received answer from BoxB: " << answer << '\n';
            } catch (const std::exception& ex) {
                std::cerr << "Node A failed to get answer: " << ex.what() << '\n';
            }

            std::this_thread::sleep_for(1s);
        }
    });

    std::jthread worker_b([&](std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            auto box_a = node_b.At("BoxA");
            auto box_b = node_b.At("BoxB");

            box_b->TryListen("HelloMsg", [](const std::string& content) {
                std::cout << "BoxB received Hello with content: " << content << '\n';
            });

            box_a->Tell("HelloMsg", "Greetings from node B");

            box_b->TryAnswer("WhatIsYourName", [](const std::string& question_content) {
                std::cout << "BoxB received question: " << question_content << '\n';
                return minx::zmesh::Answer{
                    .content_type = "NameAnswer",
                    .content = "I am BoxB",
                };
            });

            std::this_thread::sleep_for(1s);
        }
    });

    std::this_thread::sleep_for(120s);

    worker_a.request_stop();
    worker_b.request_stop();

    return 0;
}
