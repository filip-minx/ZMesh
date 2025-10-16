#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace zmesh
{
    class JsonSerializer
    {
    public:
        template <typename T>
        std::string serialize(const T& value) const
        {
            nlohmann::json json = value;
            return json.dump();
        }

        template <typename T>
        T deserialize(const std::string& data) const
        {
            auto json = nlohmann::json::parse(data);
            return json.get<T>();
        }
    };
}

