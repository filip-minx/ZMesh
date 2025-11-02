#pragma once

#include "AbstractMessageBox.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Minx::ZMesh
{
    class ZMesh
    {
    public:
        struct MessageBoxConfiguration
        {
            std::string endpoint;
            AbstractMessageBox::ConnectionMode mode{AbstractMessageBox::ConnectionMode::Connect};
        };

        using MessageBoxFactory = std::function<std::shared_ptr<AbstractMessageBox>(const MessageBoxConfiguration&)>;

        explicit ZMesh(std::unordered_map<std::string, MessageBoxConfiguration> configurations,
                       MessageBoxFactory factory = {});

        [[nodiscard]] std::shared_ptr<IAbstractMessageBox> At(const std::string& name);

    private:
        std::shared_ptr<IAbstractMessageBox> CreateMessageBox(const MessageBoxConfiguration& configuration);

        std::unordered_map<std::string, MessageBoxConfiguration> configurations_;
        MessageBoxFactory factory_;
        std::mutex mutex_;
        std::unordered_map<std::string, std::weak_ptr<IAbstractMessageBox>> message_boxes_;
    };
}
