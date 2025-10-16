#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace nlohmann
{
    template <typename T, typename U = void>
    struct adl_serializer;

    namespace detail
    {
        template <typename T, typename = void>
        struct is_complete : std::false_type
        {
        };

        template <typename T>
        struct is_complete<T, decltype(void(sizeof(T)))> : std::true_type
        {
        };

        [[noreturn]] inline void throw_parse_error(const std::string& message)
        {
            throw std::runtime_error("json parse error: " + message);
        }

        inline void skip_whitespace(const std::string& input, std::size_t& index)
        {
            while (index < input.size() && std::isspace(static_cast<unsigned char>(input[index])))
            {
                ++index;
            }
        }

        inline std::string parse_string(const std::string& input, std::size_t& index)
        {
            if (input[index] != '"')
            {
                throw_parse_error("expected opening quote to begin string");
            }
            ++index;

            std::string result;
            while (index < input.size())
            {
                char ch = input[index++];
                if (ch == '"')
                {
                    return result;
                }
                if (ch == '\\')
                {
                    if (index >= input.size())
                    {
                        throw_parse_error("incomplete escape sequence");
                    }
                    char esc = input[index++];
                    switch (esc)
                    {
                    case '"':
                    case '\\':
                    case '/':
                        result.push_back(esc);
                        break;
                    case 'b':
                        result.push_back('\b');
                        break;
                    case 'f':
                        result.push_back('\f');
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    case 'u':
                    {
                        if (index + 4 > input.size())
                        {
                            throw_parse_error("invalid unicode escape");
                        }
                        unsigned int codepoint = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            char hex = input[index++];
                            codepoint <<= 4;
                            if (hex >= '0' && hex <= '9')
                            {
                                codepoint += static_cast<unsigned int>(hex - '0');
                            }
                            else if (hex >= 'a' && hex <= 'f')
                            {
                                codepoint += static_cast<unsigned int>(hex - 'a' + 10);
                            }
                            else if (hex >= 'A' && hex <= 'F')
                            {
                                codepoint += static_cast<unsigned int>(hex - 'A' + 10);
                            }
                            else
                            {
                                throw_parse_error("invalid unicode escape");
                            }
                        }

                        if (codepoint <= 0x7F)
                        {
                            result.push_back(static_cast<char>(codepoint));
                        }
                        else if (codepoint <= 0x7FF)
                        {
                            result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        }
                        else
                        {
                            result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        }
                        break;
                    }
                    default:
                        throw_parse_error("invalid escape sequence");
                    }
                }
                else
                {
                    result.push_back(ch);
                }
            }
            throw_parse_error("unterminated string");
        }

        inline double parse_number(const std::string& input, std::size_t& index, bool& is_integer, std::int64_t& integer_value)
        {
            std::size_t start = index;
            bool is_negative = false;
            if (input[index] == '-')
            {
                is_negative = true;
                ++index;
            }
            if (index >= input.size())
            {
                throw_parse_error("unexpected end while parsing number");
            }
            if (input[index] == '0')
            {
                ++index;
            }
            else if (input[index] >= '1' && input[index] <= '9')
            {
                while (index < input.size() && std::isdigit(static_cast<unsigned char>(input[index])))
                {
                    ++index;
                }
            }
            else
            {
                throw_parse_error("invalid number");
            }

            bool has_fraction = false;
            if (index < input.size() && input[index] == '.')
            {
                has_fraction = true;
                ++index;
                if (index >= input.size() || !std::isdigit(static_cast<unsigned char>(input[index])))
                {
                    throw_parse_error("expected digit after decimal point");
                }
                while (index < input.size() && std::isdigit(static_cast<unsigned char>(input[index])))
                {
                    ++index;
                }
            }

            bool has_exponent = false;
            if (index < input.size() && (input[index] == 'e' || input[index] == 'E'))
            {
                has_exponent = true;
                ++index;
                if (index < input.size() && (input[index] == '+' || input[index] == '-'))
                {
                    ++index;
                }
                if (index >= input.size() || !std::isdigit(static_cast<unsigned char>(input[index])))
                {
                    throw_parse_error("expected exponent digits");
                }
                while (index < input.size() && std::isdigit(static_cast<unsigned char>(input[index])))
                {
                    ++index;
                }
            }

            is_integer = !has_fraction && !has_exponent;
            const std::string number_text = input.substr(start, index - start);
            char* end_ptr = nullptr;
            double number = std::strtod(number_text.c_str(), &end_ptr);
            if (end_ptr == nullptr || *end_ptr != '\0')
            {
                throw_parse_error("failed to parse number");
            }
            if (is_integer)
            {
                long long value = std::strtoll(number_text.c_str(), &end_ptr, 10);
                if (end_ptr == nullptr || *end_ptr != '\0')
                {
                    throw_parse_error("failed to parse integer");
                }
                integer_value = static_cast<std::int64_t>(value);
                if (is_negative && integer_value > 0)
                {
                    integer_value = -integer_value;
                }
            }
            return number;
        }
    }

    class json
    {
    public:
        using object_t = std::map<std::string, json>;
        using array_t = std::vector<json>;
        using string_t = std::string;
        using boolean_t = bool;
        using number_float_t = double;
        using number_integer_t = std::int64_t;
        using number_unsigned_t = std::uint64_t;
        using variant_t = std::variant<std::nullptr_t, boolean_t, number_integer_t, number_unsigned_t, number_float_t, string_t, array_t, object_t>;

        class const_iterator
        {
        public:
            using internal_iterator = object_t::const_iterator;

            const_iterator() = default;
            const_iterator(const object_t* object, internal_iterator it)
                : object_(object),
                  it_(it)
            {
            }

            const json& operator*() const
            {
                return it_->second;
            }

            const json* operator->() const
            {
                return &it_->second;
            }

            const_iterator& operator++()
            {
                ++it_;
                return *this;
            }

            bool operator==(const const_iterator& other) const
            {
                return object_ == other.object_ && it_ == other.it_;
            }

            bool operator!=(const const_iterator& other) const
            {
                return !(*this == other);
            }

            const std::string& key() const
            {
                return it_->first;
            }

        private:
            const object_t* object_{nullptr};
            internal_iterator it_{};
        };

        class iterator
        {
        public:
            using internal_iterator = object_t::iterator;

            iterator() = default;
            iterator(object_t* object, internal_iterator it)
                : object_(object),
                  it_(it)
            {
            }

            json& operator*() const
            {
                return it_->second;
            }

            json* operator->() const
            {
                return &it_->second;
            }

            iterator& operator++()
            {
                ++it_;
                return *this;
            }

            bool operator==(const iterator& other) const
            {
                return object_ == other.object_ && it_ == other.it_;
            }

            bool operator!=(const iterator& other) const
            {
                return !(*this == other);
            }

            const std::string& key() const
            {
                return it_->first;
            }

        private:
            object_t* object_{nullptr};
            internal_iterator it_{};
        };

        json() = default;

        json(std::nullptr_t)
            : data_(nullptr)
        {
        }

        json(boolean_t value)
            : data_(value)
        {
        }

        json(int value)
            : data_(static_cast<number_integer_t>(value))
        {
        }

        json(long value)
            : data_(static_cast<number_integer_t>(value))
        {
        }

        json(long long value)
            : data_(static_cast<number_integer_t>(value))
        {
        }

        json(unsigned value)
            : data_(static_cast<number_unsigned_t>(value))
        {
        }

        json(unsigned long value)
            : data_(static_cast<number_unsigned_t>(value))
        {
        }

        json(unsigned long long value)
            : data_(static_cast<number_unsigned_t>(value))
        {
        }

        json(double value)
            : data_(value)
        {
        }

        json(const string_t& value)
            : data_(value)
        {
        }

        json(string_t&& value)
            : data_(std::move(value))
        {
        }

        json(const char* value)
            : data_(string_t(value))
        {
        }

        json(array_t value)
            : data_(std::move(value))
        {
        }

        json(object_t value)
            : data_(std::move(value))
        {
        }

        json(std::initializer_list<std::pair<const std::string, json>> init)
            : data_(object_t{})
        {
            object_t obj;
            for (const auto& item : init)
            {
                obj[item.first] = item.second;
            }
            data_ = std::move(obj);
        }

        template <typename T, typename std::enable_if<!std::is_same<json, typename std::decay<T>::type>::value && !std::is_same<object_t, typename std::decay<T>::type>::value && !std::is_same<array_t, typename std::decay<T>::type>::value && !std::is_same<string_t, typename std::decay<T>::type>::value && !std::is_arithmetic<typename std::decay<T>::type>::value, int>::type = 0>
        json(const T& value)
        {
            adl_serializer<T>::to_json(*this, value);
        }

        json& operator=(std::nullptr_t)
        {
            data_ = nullptr;
            return *this;
        }

        json& operator=(boolean_t value)
        {
            data_ = value;
            return *this;
        }

        json& operator=(int value)
        {
            data_ = static_cast<number_integer_t>(value);
            return *this;
        }

        json& operator=(long value)
        {
            data_ = static_cast<number_integer_t>(value);
            return *this;
        }

        json& operator=(long long value)
        {
            data_ = static_cast<number_integer_t>(value);
            return *this;
        }

        json& operator=(unsigned value)
        {
            data_ = static_cast<number_unsigned_t>(value);
            return *this;
        }

        json& operator=(unsigned long value)
        {
            data_ = static_cast<number_unsigned_t>(value);
            return *this;
        }

        json& operator=(unsigned long long value)
        {
            data_ = static_cast<number_unsigned_t>(value);
            return *this;
        }

        json& operator=(double value)
        {
            data_ = value;
            return *this;
        }

        json& operator=(const string_t& value)
        {
            data_ = value;
            return *this;
        }

        json& operator=(string_t&& value)
        {
            data_ = std::move(value);
            return *this;
        }

        json& operator=(const char* value)
        {
            data_ = string_t(value);
            return *this;
        }

        template <typename T, typename std::enable_if<!std::is_same<json, typename std::decay<T>::type>::value && !std::is_arithmetic<typename std::decay<T>::type>::value && !std::is_same<string_t, typename std::decay<T>::type>::value, int>::type = 0>
        json& operator=(const T& value)
        {
            adl_serializer<T>::to_json(*this, value);
            return *this;
        }

        bool is_null() const noexcept
        {
            return std::holds_alternative<std::nullptr_t>(data_);
        }

        bool is_object() const noexcept
        {
            return std::holds_alternative<object_t>(data_);
        }

        bool is_array() const noexcept
        {
            return std::holds_alternative<array_t>(data_);
        }

        bool is_string() const noexcept
        {
            return std::holds_alternative<string_t>(data_);
        }

        bool is_boolean() const noexcept
        {
            return std::holds_alternative<boolean_t>(data_);
        }

        iterator begin()
        {
            if (!is_object())
            {
                throw std::runtime_error("not an object");
            }
            auto& object = std::get<object_t>(data_);
            return iterator(&object, object.begin());
        }

        iterator end()
        {
            if (!is_object())
            {
                throw std::runtime_error("not an object");
            }
            auto& object = std::get<object_t>(data_);
            return iterator(&object, object.end());
        }

        const_iterator begin() const
        {
            if (!is_object())
            {
                return const_iterator();
            }
            const auto& object = std::get<object_t>(data_);
            return const_iterator(&object, object.begin());
        }

        const_iterator end() const
        {
            if (!is_object())
            {
                return const_iterator();
            }
            const auto& object = std::get<object_t>(data_);
            return const_iterator(&object, object.end());
        }

        const_iterator find(const std::string& key) const
        {
            if (!is_object())
            {
                return const_iterator();
            }
            const auto& object = std::get<object_t>(data_);
            return const_iterator(&object, object.find(key));
        }

        iterator find(const std::string& key)
        {
            if (!is_object())
            {
                return iterator();
            }
            auto& object = std::get<object_t>(data_);
            return iterator(&object, object.find(key));
        }

        json& operator[](const std::string& key)
        {
            if (!is_object())
            {
                data_ = object_t{};
            }
            auto& object = std::get<object_t>(data_);
            return object[key];
        }

        json& operator[](const char* key)
        {
            return (*this)[std::string(key)];
        }

        const json& at(const std::string& key) const
        {
            if (!is_object())
            {
                throw std::out_of_range("json value is not an object");
            }
            const auto& object = std::get<object_t>(data_);
            auto it = object.find(key);
            if (it == object.end())
            {
                throw std::out_of_range("key not found");
            }
            return it->second;
        }

        json& at(const std::string& key)
        {
            if (!is_object())
            {
                throw std::out_of_range("json value is not an object");
            }
            auto& object = std::get<object_t>(data_);
            auto it = object.find(key);
            if (it == object.end())
            {
                throw std::out_of_range("key not found");
            }
            return it->second;
        }

        template <typename T>
        T value(const std::string& key, T default_value) const
        {
            auto it = find(key);
            if (it == end())
            {
                return default_value;
            }
            if (it->is_null())
            {
                return default_value;
            }
            return it->template get<T>();
        }

        std::string dump() const
        {
            std::ostringstream oss;
            dump_impl(oss);
            return oss.str();
        }

        template <typename T>
        T get() const
        {
            return adl_serializer<T>::from_json(*this);
        }

        template <typename T>
        void get_to(T& value) const
        {
            value = get<T>();
        }

        static json parse(const std::string& input)
        {
            std::size_t index = 0;
            detail::skip_whitespace(input, index);
            json result = parse_value(input, index);
            detail::skip_whitespace(input, index);
            if (index != input.size())
            {
                detail::throw_parse_error("unexpected trailing data");
            }
            return result;
        }

    private:
        variant_t data_{};

        static json parse_value(const std::string& input, std::size_t& index)
        {
            if (index >= input.size())
            {
                detail::throw_parse_error("unexpected end of input");
            }

            const char ch = input[index];
            switch (ch)
            {
            case 'n':
                return parse_null(input, index);
            case 't':
            case 'f':
                return parse_boolean(input, index);
            case '"':
                return json(detail::parse_string(input, index));
            case '[':
                return parse_array(input, index);
            case '{':
                return parse_object(input, index);
            default:
                if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)))
                {
                    return parse_number(input, index);
                }
                detail::throw_parse_error("unexpected character");
            }
        }

        static json parse_null(const std::string& input, std::size_t& index)
        {
            if (input.compare(index, 4, "null") != 0)
            {
                detail::throw_parse_error("expected 'null'");
            }
            index += 4;
            return json(nullptr);
        }

        static json parse_boolean(const std::string& input, std::size_t& index)
        {
            if (input.compare(index, 4, "true") == 0)
            {
                index += 4;
                return json(true);
            }
            if (input.compare(index, 5, "false") == 0)
            {
                index += 5;
                return json(false);
            }
            detail::throw_parse_error("expected boolean");
        }

        static json parse_number(const std::string& input, std::size_t& index)
        {
            bool is_integer = false;
            std::int64_t integer_value = 0;
            const auto value = detail::parse_number(input, index, is_integer, integer_value);
            if (is_integer)
            {
                return json(integer_value);
            }
            return json(value);
        }

        static json parse_array(const std::string& input, std::size_t& index)
        {
            if (input[index] != '[')
            {
                detail::throw_parse_error("expected '['");
            }
            ++index;
            detail::skip_whitespace(input, index);

            array_t array;
            if (index < input.size() && input[index] == ']')
            {
                ++index;
                return json(array);
            }

            while (index < input.size())
            {
                detail::skip_whitespace(input, index);
                array.push_back(parse_value(input, index));
                detail::skip_whitespace(input, index);
                if (index >= input.size())
                {
                    detail::throw_parse_error("unexpected end of array");
                }
                if (input[index] == ',')
                {
                    ++index;
                    continue;
                }
                if (input[index] == ']')
                {
                    ++index;
                    break;
                }
                detail::throw_parse_error("expected ',' or ']'");
            }
            return json(array);
        }

        static json parse_object(const std::string& input, std::size_t& index)
        {
            if (input[index] != '{')
            {
                detail::throw_parse_error("expected '{'");
            }
            ++index;
            detail::skip_whitespace(input, index);

            object_t object;
            if (index < input.size() && input[index] == '}')
            {
                ++index;
                return json(object);
            }

            while (index < input.size())
            {
                detail::skip_whitespace(input, index);
                if (index >= input.size() || input[index] != '"')
                {
                    detail::throw_parse_error("expected string key");
                }
                std::string key = detail::parse_string(input, index);
                detail::skip_whitespace(input, index);
                if (index >= input.size() || input[index] != ':')
                {
                    detail::throw_parse_error("expected ':'");
                }
                ++index;
                detail::skip_whitespace(input, index);
                json value = parse_value(input, index);
                object.emplace(std::move(key), std::move(value));
                detail::skip_whitespace(input, index);
                if (index >= input.size())
                {
                    detail::throw_parse_error("unexpected end of object");
                }
                if (input[index] == ',')
                {
                    ++index;
                    continue;
                }
                if (input[index] == '}')
                {
                    ++index;
                    break;
                }
                detail::throw_parse_error("expected ',' or '}'");
            }
            return json(object);
        }

        void dump_impl(std::ostringstream& oss) const
        {
            if (std::holds_alternative<std::nullptr_t>(data_))
            {
                oss << "null";
            }
            else if (std::holds_alternative<boolean_t>(data_))
            {
                oss << (std::get<boolean_t>(data_) ? "true" : "false");
            }
            else if (std::holds_alternative<number_integer_t>(data_))
            {
                oss << std::get<number_integer_t>(data_);
            }
            else if (std::holds_alternative<number_unsigned_t>(data_))
            {
                oss << std::get<number_unsigned_t>(data_);
            }
            else if (std::holds_alternative<number_float_t>(data_))
            {
                oss << std::get<number_float_t>(data_);
            }
            else if (std::holds_alternative<string_t>(data_))
            {
                oss << '"' << escape(std::get<string_t>(data_)) << '"';
            }
            else if (std::holds_alternative<array_t>(data_))
            {
                oss << '[';
                const auto& array = std::get<array_t>(data_);
                for (std::size_t i = 0; i < array.size(); ++i)
                {
                    if (i > 0)
                    {
                        oss << ',';
                    }
                    array[i].dump_impl(oss);
                }
                oss << ']';
            }
            else
            {
                oss << '{';
                const auto& object = std::get<object_t>(data_);
                bool first = true;
                for (const auto& kvp : object)
                {
                    if (!first)
                    {
                        oss << ',';
                    }
                    first = false;
                    oss << '"' << escape(kvp.first) << '"' << ':';
                    kvp.second.dump_impl(oss);
                }
                oss << '}';
            }
        }

        static std::string escape(const std::string& input)
        {
            std::string result;
            result.reserve(input.size());
            for (char ch : input)
            {
                switch (ch)
                {
                case '\"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20)
                    {
                        constexpr char hex_digits[] = "0123456789ABCDEF";
                        result += "\\u";
                        unsigned char value = static_cast<unsigned char>(ch);
                        result += '0';
                        result += '0';
                        result += hex_digits[(value >> 4) & 0x0F];
                        result += hex_digits[value & 0x0F];
                    }
                    else
                    {
                        result.push_back(ch);
                    }
                }
            }
            return result;
        }

        template <typename T, typename U>
        friend struct adl_serializer;
    };

    template <typename T, typename U>
    struct adl_serializer
    {
        static void to_json(json& j, const T& value)
        {
            static_assert(detail::is_complete<T>::value == false, "adl_serializer needs to be specialized for this type");
            (void)j;
            (void)value;
        }

        static T from_json(const json& j)
        {
            static_assert(detail::is_complete<T>::value == false, "adl_serializer needs to be specialized for this type");
            (void)j;
            return T{};
        }
    };

    template <>
    struct adl_serializer<std::string>
    {
        static void to_json(json& j, const std::string& value)
        {
            j.data_ = json::string_t(value);
        }

        static std::string from_json(const json& j)
        {
            if (!j.is_string())
            {
                throw std::runtime_error("json value is not a string");
            }
            return std::get<json::string_t>(j.data_);
        }
    };

    template <>
    struct adl_serializer<const char*>
    {
        static void to_json(json& j, const char* value)
        {
            j.data_ = json::string_t(value ? value : "");
        }

        static const char* from_json(const json&)
        {
            throw std::runtime_error("cannot convert json to const char*");
        }
    };

    template <>
    struct adl_serializer<bool>
    {
        static void to_json(json& j, bool value)
        {
            j.data_ = json::boolean_t(value);
        }

        static bool from_json(const json& j)
        {
            if (!j.is_boolean())
            {
                throw std::runtime_error("json value is not a boolean");
            }
            return std::get<json::boolean_t>(j.data_);
        }
    };

    template <>
    struct adl_serializer<int>
    {
        static void to_json(json& j, int value)
        {
            j.data_ = json::number_integer_t(value);
        }

        static int from_json(const json& j)
        {
            if (std::holds_alternative<json::number_integer_t>(j.data_))
            {
                return static_cast<int>(std::get<json::number_integer_t>(j.data_));
            }
            if (std::holds_alternative<json::number_unsigned_t>(j.data_))
            {
                return static_cast<int>(std::get<json::number_unsigned_t>(j.data_));
            }
            if (std::holds_alternative<json::number_float_t>(j.data_))
            {
                return static_cast<int>(std::get<json::number_float_t>(j.data_));
            }
            throw std::runtime_error("json value is not a number");
        }
    };

    template <>
    struct adl_serializer<long long>
    {
        static void to_json(json& j, long long value)
        {
            j.data_ = json::number_integer_t(value);
        }

        static long long from_json(const json& j)
        {
            if (std::holds_alternative<json::number_integer_t>(j.data_))
            {
                return std::get<json::number_integer_t>(j.data_);
            }
            if (std::holds_alternative<json::number_unsigned_t>(j.data_))
            {
                return static_cast<long long>(std::get<json::number_unsigned_t>(j.data_));
            }
            if (std::holds_alternative<json::number_float_t>(j.data_))
            {
                return static_cast<long long>(std::get<json::number_float_t>(j.data_));
            }
            throw std::runtime_error("json value is not a number");
        }
    };

    template <>
    struct adl_serializer<unsigned long long>
    {
        static void to_json(json& j, unsigned long long value)
        {
            j.data_ = json::number_unsigned_t(value);
        }

        static unsigned long long from_json(const json& j)
        {
            if (std::holds_alternative<json::number_unsigned_t>(j.data_))
            {
                return std::get<json::number_unsigned_t>(j.data_);
            }
            if (std::holds_alternative<json::number_integer_t>(j.data_))
            {
                return static_cast<unsigned long long>(std::get<json::number_integer_t>(j.data_));
            }
            if (std::holds_alternative<json::number_float_t>(j.data_))
            {
                return static_cast<unsigned long long>(std::get<json::number_float_t>(j.data_));
            }
            throw std::runtime_error("json value is not a number");
        }
    };

    template <>
    struct adl_serializer<double>
    {
        static void to_json(json& j, double value)
        {
            j.data_ = json::number_float_t(value);
        }

        static double from_json(const json& j)
        {
            if (std::holds_alternative<json::number_float_t>(j.data_))
            {
                return std::get<json::number_float_t>(j.data_);
            }
            if (std::holds_alternative<json::number_integer_t>(j.data_))
            {
                return static_cast<double>(std::get<json::number_integer_t>(j.data_));
            }
            if (std::holds_alternative<json::number_unsigned_t>(j.data_))
            {
                return static_cast<double>(std::get<json::number_unsigned_t>(j.data_));
            }
            throw std::runtime_error("json value is not a number");
        }
    };

    template <typename T>
    struct adl_serializer<std::vector<T>>
    {
        static void to_json(json& j, const std::vector<T>& value)
        {
            typename json::array_t array;
            array.reserve(value.size());
            for (const auto& element : value)
            {
                json item;
                adl_serializer<T>::to_json(item, element);
                array.emplace_back(std::move(item));
            }
            j.data_ = json::array_t(std::move(array));
        }

        static std::vector<T> from_json(const json& j)
        {
            if (!j.is_array())
            {
                throw std::runtime_error("json value is not an array");
            }
            const auto& array = std::get<json::array_t>(j.data_);
            std::vector<T> result;
            result.reserve(array.size());
            for (const auto& element : array)
            {
                result.push_back(adl_serializer<T>::from_json(element));
            }
            return result;
        }
    };

    template <typename T>
    struct adl_serializer<std::map<std::string, T>>
    {
        static void to_json(json& j, const std::map<std::string, T>& value)
        {
            typename json::object_t object;
            for (const auto& kvp : value)
            {
                json element;
                adl_serializer<T>::to_json(element, kvp.second);
                object.emplace(kvp.first, std::move(element));
            }
            j.data_ = json::object_t(std::move(object));
        }

        static std::map<std::string, T> from_json(const json& j)
        {
            if (!j.is_object())
            {
                throw std::runtime_error("json value is not an object");
            }
            const auto& object = std::get<json::object_t>(j.data_);
            std::map<std::string, T> result;
            for (const auto& kvp : object)
            {
                result.emplace(kvp.first, adl_serializer<T>::from_json(kvp.second));
            }
            return result;
        }
    };

    template <typename T>
    struct adl_serializer<std::optional<T>>
    {
        static void to_json(json& j, const std::optional<T>& value)
        {
            if (!value)
            {
                j = nullptr;
                return;
            }
            json inner;
            adl_serializer<T>::to_json(inner, *value);
            j = inner;
        }

        static std::optional<T> from_json(const json& j)
        {
            if (j.is_null())
            {
                return std::nullopt;
            }
            return adl_serializer<T>::from_json(j);
        }
    };

} // namespace nlohmann

