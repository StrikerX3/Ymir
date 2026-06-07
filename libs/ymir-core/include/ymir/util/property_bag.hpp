#pragma once

/**
@file
@brief Defines `PropertyBag`, a container for strongly-typed properties.

# Usage

Define keys by creating tag types that extend `Property<T>`:

```cpp
struct MaxCounter : util::Property<int> {
    static constexpr int kDefaultValue = 1; // optional default value for GetOrDefault
};
struct UserName : util::Property<std::string> {};
```

Create a property bag and use the keys to set and get properties:

```cpp
util::PropertyBag bag{};
bag.Set<MaxCounter>(100); // takes `int`
bag.Set<UserName>("john-smith"); // takes `std::string`

const int maxCounter = bag.Get<MaxCounter>(); // returns `int` (= 100)
const std::string username = bag.Get<UserName>(); // returns `std::string` (= "john-smith")
```
*/

#include <memory>
#include <optional>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

namespace util {

/// @brief Specifies the type of a property key.
/// @tparam T the type of the property
template <typename T>
struct Property {
    using value_type = T;
};

/// @brief Selects types that represent property keys for a property bag.
template <typename Key>
concept PropertyKey = requires {
    typename Key::value_type;
    requires std::is_base_of_v<Property<typename Key::value_type>, Key>;
};

/// @brief Selects types that represent property keys for a property bag that have a default value specified in the
/// marker type itself.
template <typename Key>
concept PropertyKeyWithDefault =
    PropertyKey<Key> && std::same_as<std::decay_t<decltype(Key::kDefaultValue)>, typename Key::value_type>;

// ---------------------------------------------------------------------------------------------------------------------

/// @brief Holds a set of strongly-typed properties using templated keys.
class PropertyBag {
    /// @brief Holds a property's value.
    struct IPropertyHolder {
        virtual ~IPropertyHolder() = default;
    };

    /// @brief Holds a property's value.
    /// @tparam T the property type
    template <typename T>
    struct PropertyHolder : IPropertyHolder {
        T value;

        PropertyHolder(const T &value)
            : value(value) {}
    };

public:
    /// @brief Retrieves a property from the bag.
    /// @tparam TKey the property key
    /// @return the value of the property if present, `std::nullopt` otherwise
    template <PropertyKey TKey>
    std::optional<typename TKey::value_type> Get() const {
        auto it = m_data.find(std::type_index(typeid(TKey)));
        if (it == m_data.end()) {
            return std::nullopt;
        }

        using T = typename TKey::value_type;
        auto *holder = static_cast<PropertyHolder<T> *>(it->second.get());
        return holder->value;
    }

    /// @brief Retrieves a property from the bag with a fallback default value if absent.
    /// @tparam TKey the property key
    /// @param[in] fallback the fallback value to return if the property is absent
    /// @return the value of the property, or the fallback value if absent
    template <PropertyKey TKey>
    typename TKey::value_type GetOr(const typename TKey::value_type &fallback) const {
        auto it = m_data.find(std::type_index(typeid(TKey)));
        if (it == m_data.end()) {
            return fallback;
        }

        using T = typename TKey::value_type;
        auto *holder = static_cast<PropertyHolder<T> *>(it->second.get());
        return holder->value;
    }

    /// @brief Retrieves a property from the bag with a fallback default value if absent.
    /// @tparam TKey the property key (with a default value)
    /// @param[in] fallback the fallback value to return if the property is absent
    /// @return the value of the property, or the fallback value if absent
    template <PropertyKeyWithDefault TKey>
    typename TKey::value_type GetOrDefault(const typename TKey::value_type &fallback = TKey::kDefaultValue) const {
        auto it = m_data.find(std::type_index(typeid(TKey)));
        if (it == m_data.end()) {
            return fallback;
        }

        using T = typename TKey::value_type;
        auto *holder = static_cast<PropertyHolder<T> *>(it->second.get());
        return holder->value;
    }

    /// @brief Sets the value of a property.
    /// @tparam TKey the property key
    /// @param[in] value the value to set
    template <PropertyKey TKey>
    void Set(const typename TKey::value_type &value) {
        using T = typename TKey::value_type;
        m_data[std::type_index(typeid(TKey))] = std::make_unique<PropertyHolder<T>>(value);
    }

    /// @brief Clears a property.
    /// @tparam TKey the property key
    template <PropertyKey TKey>
    void Clear() {
        using T = typename TKey::value_type;
        m_data.erase(std::type_index(typeid(TKey)));
    }

    /// @brief Clears all properties.
    void ClearAll() {
        m_data.clear();
    }

    /// @brief Gets a property from a bag with a fallback value if the property is not set or the bag is null.
    /// @tparam TKey the property key
    /// @param[in,opt] bag a pointer to the bag
    /// @return the value of the property if present, `std::nullopt` otherwise
    template <PropertyKey TKey>
    static std::optional<typename TKey::value_type> NullSafeGet(const PropertyBag *bag) {
        if (bag == nullptr) {
            return std::nullopt;
        }
        return bag->Get<TKey>();
    }

    /// @brief Gets a property from a bag with a fallback value if the property is not set or the bag is null.
    /// @tparam TKey the property key
    /// @param[in,opt] bag a pointer to the bag
    /// @param[in] fallback the fallback value to return if the property is absent
    /// @return the value of the property, or the fallback value if absent
    template <PropertyKey TKey>
    static typename TKey::value_type NullSafeGetOrDefault(const PropertyBag *bag,
                                                          const typename TKey::value_type &fallback) {
        if (bag == nullptr) {
            return fallback;
        }
        return bag->GetOr<TKey>(fallback);
    }

    /// @brief Gets a property from a bag with a fallback value if the property is not set or the bag is null.
    /// @tparam TKey the property key
    /// @param[in,opt] bag a pointer to the bag
    /// @param[in] fallback the fallback value to return if the property is absent
    /// @return the value of the property, or the fallback value if absent
    template <PropertyKeyWithDefault TKey>
    static typename TKey::value_type
    NullSafeGetOrDefault(const PropertyBag *bag, const typename TKey::value_type &fallback = TKey::kDefaultValue) {
        if (bag == nullptr) {
            return fallback;
        }
        return bag->GetOrDefault<TKey>(fallback);
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<IPropertyHolder>> m_data;
};

} // namespace util
