#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace zmesh::uuid {

inline std::string generate() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;

    std::array<unsigned char, 16> data{};
    auto first = dist(engine);
    auto second = dist(engine);

    for (std::size_t i = 0; i < 8; ++i) {
        data[i] = static_cast<unsigned char>((first >> (i * 8)) & 0xFF);
        data[i + 8] = static_cast<unsigned char>((second >> (i * 8)) & 0xFF);
    }

    data[6] = static_cast<unsigned char>((data[6] & 0x0F) | 0x40); // Version 4
    data[8] = static_cast<unsigned char>((data[8] & 0x3F) | 0x80); // Variant 1

    constexpr auto hex = "0123456789abcdef";
    std::string result(36, '-');
    std::size_t index = 0;
    auto append_byte = [&](unsigned char value) {
        result[index++] = hex[value >> 4];
        result[index++] = hex[value & 0x0F];
    };

    index = 0;
    append_byte(data[0]);
    append_byte(data[1]);
    append_byte(data[2]);
    append_byte(data[3]);
    index = 8;
    append_byte(data[4]);
    append_byte(data[5]);
    index = 13;
    append_byte(data[6]);
    append_byte(data[7]);
    index = 18;
    append_byte(data[8]);
    append_byte(data[9]);
    index = 23;
    append_byte(data[10]);
    append_byte(data[11]);
    append_byte(data[12]);
    append_byte(data[13]);
    append_byte(data[14]);
    append_byte(data[15]);

    return result;
}

} // namespace zmesh::uuid

