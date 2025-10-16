#pragma once

#include "message_box.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>

#if defined(__GNUG__)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace zmesh
{
    namespace detail
    {
#if defined(__GNUG__)
        inline std::string demangle(const char* name)
        {
            int status = 0;
            std::unique_ptr<char, void (*)(void*)> result{
                abi::__cxa_demangle(name, nullptr, nullptr, &status),
                std::free};
            if (status == 0 && result)
            {
                return std::string(result.get());
            }

            return std::string{name};
        }
#else
        inline std::string demangle(const char* name)
        {
            return std::string{name};
        }
#endif
    }

    template <typename T>
    struct type_name
    {
        static std::string value()
        {
            return detail::demangle(typeid(T).name());
        }
    };

    template <typename Serializer>
    class TypedMessageBox
    {
    public:
        TypedMessageBox(std::shared_ptr<MessageBox> inner, Serializer serializer)
            : inner_(std::move(inner)), serializer_(std::move(serializer))
        {
        }

        template <typename TQuestion, typename TAnswer>
        TAnswer ask(const TQuestion& question,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        {
            auto serialized_question = serializer_.template serialize<TQuestion>(question);
            auto answer = inner_->ask(type_name<TQuestion>::value(), serialized_question, timeout);
            return serializer_.template deserialize<TAnswer>(answer.content);
        }

        template <typename TQuestion, typename TAnswer>
        TAnswer ask(std::chrono::milliseconds timeout = std::chrono::milliseconds{0})
        {
            return ask<TQuestion, TAnswer>(TQuestion{}, timeout);
        }

        template <typename TQuestion, typename TAnswer>
        TAnswer ask()
        {
            return ask<TQuestion, TAnswer>(TQuestion{}, std::chrono::milliseconds{0});
        }

        template <typename TQuestion, typename TAnswer>
        bool try_answer(const std::function<TAnswer(const TQuestion&)>& handler)
        {
            return inner_->try_answer(type_name<TQuestion>::value(),
                                      [this, handler](const std::optional<std::string>& payload) -> Answer
                                      {
                                          TQuestion deserialized = payload ? serializer_.template deserialize<TQuestion>(*payload)
                                                                           : TQuestion{};
                                          auto answer_value = handler(deserialized);
                                          return Answer{type_name<TAnswer>::value(), serializer_.template serialize<TAnswer>(answer_value)};
                                      });
        }

        template <typename TMessage>
        void tell(const TMessage& message)
        {
            inner_->tell(type_name<TMessage>::value(), serializer_.template serialize<TMessage>(message));
        }

        template <typename TMessage>
        bool try_listen(const std::function<void(const TMessage&)>& handler)
        {
            return inner_->try_listen(type_name<TMessage>::value(),
                                      [this, handler](const std::string& payload)
                                      {
                                          auto deserialized = serializer_.template deserialize<TMessage>(payload);
                                          handler(deserialized);
                                      });
        }

        std::shared_ptr<MessageBox> inner() const
        {
            return inner_;
        }

    private:
        std::shared_ptr<MessageBox> inner_;
        Serializer serializer_;
    };
}

