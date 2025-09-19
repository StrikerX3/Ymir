#pragma once

/**
@file
@brief Defines `ymir::sys::Bus`, a memory bus interconnecting various components in the system.
*/

#include <ymir/core/types.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/function_info.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/type_traits_ex.hpp>
#include <ymir/util/unreachable.hpp>

#include <concepts>
#include <type_traits>

namespace ymir::sys {

using FnRead8 = uint8 (*)(uint32 address, void *ctx);   ///< Function signature for 8-bit reads.
using FnRead16 = uint16 (*)(uint32 address, void *ctx); ///< Function signature for 16-bit reads.
using FnRead32 = uint32 (*)(uint32 address, void *ctx); ///< Function signature for 32-bit reads.

using FnWrite8 = void (*)(uint32 address, uint8 value, void *ctx);   ///< Function signature for 8-bit writes.
using FnWrite16 = void (*)(uint32 address, uint16 value, void *ctx); ///< Function signature for 16-bit writes.
using FnWrite32 = void (*)(uint32 address, uint32 value, void *ctx); ///< Function signature for 32-bit writes.

/// @brief Specifies valid bus handler function types.
/// @tparam T the type to check
template <typename T>
concept bus_handler_fn =
    fninfo::IsAssignable<FnRead8, T> || fninfo::IsAssignable<FnRead16, T> || fninfo::IsAssignable<FnRead32, T> ||
    fninfo::IsAssignable<FnWrite8, T> || fninfo::IsAssignable<FnWrite16, T> || fninfo::IsAssignable<FnWrite32, T>;

/// @brief Represents a memory bus interconnecting various components in the system.
///
/// `Read` and `Write` perform reads and writes with all side-effects and restrictions imposed by the hardware.
/// `Peek` and `Poke` bypass restrictions and don't cause any side-effects. These are meant to be used by debuggers.
///
/// `Map` methods assign read/write functions to a range of addresses. `MapNormal` refers to the regular `Read`/`Write`
/// functions and `MapSideEffectFree` refers to the `Peek`/`Poke` variants. `Unmap` clears the assignments.
///
/// @tparam addressBits number of valid address bits
template <uint32 addressBits>
class Bus {
    static constexpr uint32 kAddressMask = (1u << addressBits) - 1;
    static constexpr uint32 kPageGranularityBits = 16;
    static constexpr uint32 kPageSize = 1u << kPageGranularityBits;
    static constexpr uint32 kPageMask = kPageSize - 1;
    static constexpr uint32 kPageCount = (1u << (addressBits - kPageGranularityBits));

public:
    /// @brief Maps both normal (read/write) and side-effect-free (peek/poke) handlers to the specified range.
    ///
    /// The same handler of a given type will be used for both categories.
    ///
    /// Only the handlers of the specified types are modified; all others are left untouched.
    ///
    /// Handler types must be unique.
    ///
    /// Replaces arrays previously mapped to the region.
    ///
    /// @tparam ...THandlers the types of the handlers; automatically deduced from function arguments.
    /// @param[in] start the lower bound of the address range to map the handlers into
    /// @param[in] end the upper bound of the address range to map the handlers into
    /// @param[in] context a user pointer to any object that's passed as the context pointer to handlers
    /// @param[in] ...handlers the handlers to map
    template <bus_handler_fn... THandlers>
        requires util::unique_types<THandlers...>
    void MapBoth(uint32 start, uint32 end, void *context, THandlers &&...handlers) {
        Map<true, true, THandlers...>(start, end, context, std::forward<THandlers>(handlers)...);
    }

    /// @brief Maps normal (read/write) handlers to the specified range.
    ///
    /// Only the handlers of the specified types are modified; all others (including side-effect-free versions) are left
    /// untouched.
    ///
    /// Handler types must be unique.
    ///
    /// Replaces arrays previously mapped to the region.
    ///
    /// @tparam ...THandlers the types of the handlers; automatically deduced from function arguments.
    /// @param[in] start the lower bound of the address range to map the handlers into
    /// @param[in] end the upper bound of the address range to map the handlers into
    /// @param[in] context a user pointer to any object that's passed as the context pointer to handlers
    /// @param[in] ...handlers the handlers to map
    template <bus_handler_fn... THandlers>
        requires util::unique_types<THandlers...>
    void MapNormal(uint32 start, uint32 end, void *context, THandlers &&...handlers) {
        Map<true, false, THandlers...>(start, end, context, std::forward<THandlers>(handlers)...);
    }

    /// @brief Maps side-effect-free (peek/poke) handlers to the specified range.
    ///
    /// Only the handlers of the specified types are modified; all others (including normal versions) are left
    /// untouched.
    ///
    /// Handler types must be unique.
    ///
    /// Replaces arrays previously mapped to the region.
    ///
    /// @tparam ...THandlers the types of the handlers; automatically deduced from function arguments.
    /// @param[in] start the lower bound of the address range to map the handlers into
    /// @param[in] end the upper bound of the address range to map the handlers into
    /// @param[in] context a user pointer to any object that's passed as the context pointer to handlers
    /// @param[in] ...handlers the handlers to map
    template <bus_handler_fn... THandlers>
        requires util::unique_types<THandlers...>
    void MapSideEffectFree(uint32 start, uint32 end, void *context, THandlers &&...handlers) {
        Map<false, true, THandlers...>(start, end, context, std::forward<THandlers>(handlers)...);
    }

    /// @brief Unmaps all handlers from the specified range.
    /// @param[in] start the lower bound of the address range to unmap the handlers from
    /// @param[in] end the upper bound of the address range to unmap the handlers from
    void Unmap(uint32 start, uint32 end) {
        const uint32 startIndex = start >> kPageGranularityBits;
        const uint32 endIndex = end >> kPageGranularityBits;
        for (uint32 i = startIndex; i <= endIndex; i++) {
            m_pages[i] = {};
        }
    }

    /// @brief Convenience method that maps an array to the specified range.
    ///
    /// The array is mapped to oth normal and side-effect-free accesses and replaces all function handlers.
    ///
    /// The array is mapped from the beginning at `start` and mirrored across the whole range until `end`.
    /// If the array is larger than the range, only the range `array[0]..array[end-start-1]` is mapped.
    /// If the array is smaller than the range, the entire array is mirrored as many times as needed to fit the range.
    ///
    /// @tparam N the size of the array. Must be a power of two and at least as large as the bus's page size
    /// @param[in] start the lower bound of the address range to map the handlers into
    /// @param[in] end the upper bound of the address range to map the handlers into
    /// @param array a reference to the array to be mapped
    /// @param writable indicates if the array is meant to be writable or read-only
    template <size_t N>
        requires(bit::is_power_of_two(N) && N >= kPageSize)
    void MapArray(uint32 start, uint32 end, std::array<uint8, N> &array, bool writable) {
        static constexpr uint32 kMask = N - 1;

        const uint32 startIndex = start >> kPageGranularityBits;
        const uint32 endIndex = end >> kPageGranularityBits;
        uint32 offset = 0;
        for (uint32 i = startIndex; i <= endIndex; i++) {
            m_pages[i] = {}; // clear all handlers
            m_pages[i].array = &array[offset & kMask];
            m_pages[i].arrayWritable = writable;
            offset += kPageSize;
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Accessors

    /// @brief Reads data from the bus using the handler assigned to the specified address.
    /// @tparam T the data type of the access
    /// @param[in] address the address to read
    /// @return the value read from the handler
    template <mem_primitive T>
    FLATTEN FORCE_INLINE T Read(uint32 address) const {
        address &= kAddressMask & ~(sizeof(T) - 1);

        const MemoryPage &entry = m_pages[address >> kPageGranularityBits];

        if (entry.array) {
            return util::ReadBE<T>(&entry.array[address & kPageMask]);
        }
        if constexpr (std::is_same_v<T, uint8>) {
            return entry.read8(address, entry.ctx);
        } else if constexpr (std::is_same_v<T, uint16>) {
            return entry.read16(address, entry.ctx);
        } else if constexpr (std::is_same_v<T, uint32>) {
            return entry.read32(address, entry.ctx);
        } else {
            // should never happen
            util::unreachable();
        }
    }

    /// @brief Writes data to the bus using the handler assigned to the specified address.
    /// @tparam T the data type of the access
    /// @param[in] address the address to write
    /// @param[in] value the value to write
    template <mem_primitive T>
    FLATTEN FORCE_INLINE void Write(uint32 address, T value) {
        address &= kAddressMask & ~(sizeof(T) - 1);

        const MemoryPage &entry = m_pages[address >> kPageGranularityBits];

        if (entry.array) {
            if (entry.arrayWritable) {
                util::WriteBE<T>(&entry.array[address & kPageMask], value);
            }
            return;
        }
        if constexpr (std::is_same_v<T, uint8>) {
            entry.write8(address, value, entry.ctx);
        } else if constexpr (std::is_same_v<T, uint16>) {
            entry.write16(address, value, entry.ctx);
        } else if constexpr (std::is_same_v<T, uint32>) {
            entry.write32(address, value, entry.ctx);
        } else {
            // should never happen
            util::unreachable();
        }
    }

    /// @brief Reads data from the bus using the side-effect-free handler assigned to the specified address.
    /// @tparam T the data type of the access
    /// @param[in] address the address to read
    /// @return the value read from the handler
    template <mem_primitive T>
    FLATTEN FORCE_INLINE T Peek(uint32 address) const {
        address &= kAddressMask & ~(sizeof(T) - 1);

        const MemoryPage &entry = m_pages[address >> kPageGranularityBits];

        if (entry.array) {
            return util::ReadBE<T>(&entry.array[address & kPageMask]);
        }
        if constexpr (std::is_same_v<T, uint8>) {
            return entry.peek8(address, entry.ctx);
        } else if constexpr (std::is_same_v<T, uint16>) {
            return entry.peek16(address, entry.ctx);
        } else if constexpr (std::is_same_v<T, uint32>) {
            return entry.peek32(address, entry.ctx);
        } else {
            // should never happen
            util::unreachable();
        }
    }

    /// @brief Writes data to the bus using the side-effect-free handler assigned to the specified address.
    /// @tparam T the data type of the access
    /// @param[in] address the address to write
    /// @param[in] value the value to write
    template <mem_primitive T>
    FLATTEN FORCE_INLINE void Poke(uint32 address, T value) {
        address &= kAddressMask & ~(sizeof(T) - 1);

        const MemoryPage &entry = m_pages[address >> kPageGranularityBits];

        if (entry.array) {
            if (entry.arrayWritable) {
                util::WriteBE<T>(&entry.array[address & kPageMask], value);
            }
            return;
        }
        if constexpr (std::is_same_v<T, uint8>) {
            entry.poke8(address, value, entry.ctx);
        } else if constexpr (std::is_same_v<T, uint16>) {
            entry.poke16(address, value, entry.ctx);
        } else if constexpr (std::is_same_v<T, uint32>) {
            entry.poke32(address, value, entry.ctx);
        } else {
            // should never happen
            util::unreachable();
        }
    }

private:
    struct alignas(64) MemoryPage {
        // Fast path for simple arrays

        uint8 *array = nullptr;
        bool arrayWritable = false;

        // Slow path for MMIO and other regions

        void *ctx = nullptr;

        FnRead8 read8 = [](uint32, void *) -> uint8 { return 0; };
        FnRead16 read16 = [](uint32, void *) -> uint16 { return 0; };
        FnRead32 read32 = [](uint32, void *) -> uint32 { return 0; };

        FnWrite8 write8 = [](uint32, uint8, void *) {};
        FnWrite16 write16 = [](uint32, uint16, void *) {};
        FnWrite32 write32 = [](uint32, uint32, void *) {};

        FnRead8 peek8 = [](uint32, void *) -> uint8 { return 0; };
        FnRead16 peek16 = [](uint32, void *) -> uint16 { return 0; };
        FnRead32 peek32 = [](uint32, void *) -> uint32 { return 0; };

        FnWrite8 poke8 = [](uint32, uint8, void *) {};
        FnWrite16 poke16 = [](uint32, uint16, void *) {};
        FnWrite32 poke32 = [](uint32, uint32, void *) {};
    };
    static_assert(bit::is_power_of_two(sizeof(MemoryPage))); // in order to avoid a multiplication when indexing pages

    alignas(64) std::array<MemoryPage, kPageCount> m_pages;

    template <bool normal, bool sideEffectFree, bus_handler_fn... THandlers>
        requires util::unique_types<THandlers...>
    void Map(uint32 start, uint32 end, void *context, THandlers &&...handlers) {
        const uint32 startIndex = start >> kPageGranularityBits;
        const uint32 endIndex = end >> kPageGranularityBits;
        for (uint32 i = startIndex; i <= endIndex; i++) {
            m_pages[i].array = nullptr;
            m_pages[i].arrayWritable = false;

            m_pages[i].ctx = context;
            if constexpr (normal) {
                (AssignHandler<false>(m_pages[i], std::forward<THandlers>(handlers)), ...);
            }
            if constexpr (sideEffectFree) {
                (AssignHandler<true>(m_pages[i], std::forward<THandlers>(handlers)), ...);
            }
        }
    }

    template <bool peekpoke, bus_handler_fn THandler>
    static void AssignHandler(MemoryPage &page, THandler &&handler) {
        if constexpr (peekpoke) {
            if constexpr (fninfo::IsAssignable<FnRead8, THandler>) {
                page.peek8 = handler;
            } else if constexpr (fninfo::IsAssignable<FnRead16, THandler>) {
                page.peek16 = handler;
            } else if constexpr (fninfo::IsAssignable<FnRead32, THandler>) {
                page.peek32 = handler;
            } else if constexpr (fninfo::IsAssignable<FnWrite8, THandler>) {
                page.poke8 = handler;
            } else if constexpr (fninfo::IsAssignable<FnWrite16, THandler>) {
                page.poke16 = handler;
            } else if constexpr (fninfo::IsAssignable<FnWrite32, THandler>) {
                page.poke32 = handler;
            }
        } else {
            if constexpr (fninfo::IsAssignable<FnRead8, THandler>) {
                page.read8 = handler;
            } else if constexpr (fninfo::IsAssignable<FnRead16, THandler>) {
                page.read16 = handler;
            } else if constexpr (fninfo::IsAssignable<FnRead32, THandler>) {
                page.read32 = handler;
            } else if constexpr (fninfo::IsAssignable<FnWrite8, THandler>) {
                page.write8 = handler;
            } else if constexpr (fninfo::IsAssignable<FnWrite16, THandler>) {
                page.write16 = handler;
            } else if constexpr (fninfo::IsAssignable<FnWrite32, THandler>) {
                page.write32 = handler;
            }
        }
    }
};

using SH1Bus = Bus<28>;
using SH2Bus = Bus<27>;

} // namespace ymir::sys
