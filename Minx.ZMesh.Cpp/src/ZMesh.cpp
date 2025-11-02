#include "Minx/ZMesh/ZMesh.h"

#include <stdexcept>
#include <utility>

namespace Minx::ZMesh
{
    namespace
    {
        std::shared_ptr<AbstractMessageBox> DefaultFactory(const ZMesh::MessageBoxConfiguration& configuration)
        {
            return std::make_shared<AbstractMessageBox>(configuration.endpoint, configuration.mode);
        }
    }

    ZMesh::ZMesh(std::unordered_map<std::string, MessageBoxConfiguration> configurations,
                 MessageBoxFactory factory)
        : configurations_{std::move(configurations)}
        , factory_{std::move(factory)}
    {
        if (!factory_)
        {
            factory_ = DefaultFactory;
        }
    }

    std::shared_ptr<IAbstractMessageBox> ZMesh::At(const std::string& name)
    {
        const auto config_it = configurations_.find(name);
        if (config_it == configurations_.end())
        {
            throw std::out_of_range{"No message box configuration registered for '" + name + "'."};
        }

        std::lock_guard lock{mutex_};
        if (auto existing_it = message_boxes_.find(name); existing_it != message_boxes_.end())
        {
            if (auto existing = existing_it->second.lock())
            {
                return existing;
            }
        }

        auto created = CreateMessageBox(config_it->second);
        message_boxes_[name] = created;
        return created;
    }

    std::shared_ptr<IAbstractMessageBox> ZMesh::CreateMessageBox(const MessageBoxConfiguration& configuration)
    {
        auto concrete = factory_(configuration);
        if (!concrete)
        {
            throw std::runtime_error{"ZMesh factory failed to create an AbstractMessageBox."};
        }

        std::shared_ptr<IAbstractMessageBox> result = std::move(concrete);
        return result;
    }
}
