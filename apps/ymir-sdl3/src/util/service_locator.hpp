#pragma once

#include <typeindex>
#include <unordered_map>

namespace util {

/// @brief Throw when the application attempts to register the same service more than once.
/// @tparam TService the service type
template <typename TService>
struct ServiceCollisionError {};

/// @brief Thrown when failing to lookup a required service.
/// @tparam TService the service type
template <typename TService>
struct ServiceNotFoundError {};

/// @brief Centralizes a collection of services to be used by the application.
class ServiceLocator {
public:
    /// @brief Registers a service instance with this locator.
    /// Throws ServiceCollisionError<TService> if the service was already registered.
    /// @tparam TService the service type
    /// @param[in] service a non-owning reference to the service instance
    template <typename TService>
    void Register(TService &service) {
        if (m_services.contains(typeid(TService))) {
            throw ServiceCollisionError<TService>();
        }
        m_services[typeid(TService)] = &service;
    }

    /// @brief Unregisters a service from this locator.
    /// @tparam TService the service type
    template <typename TService>
    void Unregister() {
        m_services.erase(typeid(TService));
    }

    /// @brief Attempts to retrieve a pointer to the given service.
    /// Returns `nullptr` if the service was not registered.
    /// @tparam TService the service type
    /// @return a pointer to the service, or `nullptr` if it wasn't registered
    template <typename TService>
    TService *Get() {
        auto it = m_services.find(typeid(TService));
        if (it != m_services.end()) {
            return static_cast<TService *>(it->second);
        } else {
            return nullptr;
        }
    }

    /// @brief Attempts to retrieve a required service.
    /// Throws ServiceNotFoundError<TService> if the service was not registered.
    /// @tparam TService the service type
    /// @return a reference to the registered service
    template <typename TService>
    TService &GetRequired() {
        TService *service = Get<TService>();
        if (service == nullptr) {
            throw ServiceNotFoundError<TService>();
        }
        return *service;
    }

private:
    std::unordered_map<std::type_index, void *> m_services{};
};

} // namespace util
