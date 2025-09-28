#pragma once

/**
@file
@brief Defines `ymir::core::Scheduler`, the event scheduler.
*/

#include "scheduler_defs.hpp"

#include <ymir/state/state_scheduler.hpp>

#include <ymir/util/inline.hpp>

#include <array>
#include <cassert>
#include <limits>

namespace ymir::core {

class Scheduler;

/// @brief Contains the context for a scheduled event.
///
/// Passed as a parameter to scheduled event handlers to let them reschedule the event relative to the previous trigger.
///
/// By default, events are not rescheduled unless requested through `EventContext::Reschedule(uint64)`.
struct EventContext {
    /// @brief Reschedules the event with an offset from the previous deadline.
    /// @param[in] interval the interval in cycles from the previous deadline
    void Reschedule(uint64 interval) {
        reschedule = true;
        this->interval = interval;
    }

private:
    bool reschedule = false;
    uint64 interval = 0;
    friend class Scheduler;
};

/// @brief The event scheduler.
///
/// The scheduler is an optimization to the emulator loop when many events need to be triggered at specific points in
/// time. The naive approach is to use a simple cycle counter for each event that is decremented as emulation advances.
/// The events are triggered when the counter reaches zero. Another option is to use a global counter and use deadlines
/// instead of counting down cycles. Both of these have the disadvantage of requiring an O(n) search to determine what
/// is the next event to trigger.
///
/// This implementation of the scheduler uses absolute timestamps. It contains a primary cycle counter and events are
/// scheduled with absolute deadlines. The scheduler precomputes the closest deadline to be reached and provides this
/// information to the emulator loop so that it can run unimpeded by events for as many cycles as possible. Once the
/// deadlines are reached, the scheduler triggers the events, invoking their registered callbacks, and reschedules them
/// if necessary, also updating the next deadline.
///
/// The scheduler contains a fixed-size array of `kNumScheduledEvent` elements that must be manually registered by each
/// component that needs to handle such events. Registering is done by the `Scheduler::RegisterEvent` method that takes
/// the callback function, a user context pointer and a user ID for identifying the event in save states. The returned
/// `EventID` must be used to schedule the event with `Scheduler::ScheduleFromNow` or `Scheduler::ScheduleAt`.
///
/// The callback function takes an `EventContext` object and the user context pointer provided on registration. The
/// event context must be used to reschedule the event. Events are single-shot unless they reschedule themselves with
/// `EventContext::RescheduleFromPrevious` or `EventContext::RescheduleFromNow`.
class Scheduler {
public:
    /// @brief Callback signature for scheduled events
    using EventCallback = void (*)(EventContext &eventContext, void *userContext);

    /// @brief An event ID that represents an invalid event.
    static constexpr EventID kInvalidEvent = ~static_cast<EventID>(0);

    /// @brief Creates a new, empty scheduler.
    Scheduler() {
        m_eventPtrs.fill(kInvalidEvent);
        m_nextEventIndex = 0;
        Reset();
    }

    /// @brief Resets the scheduler's current and target counters.
    void Reset() {
        m_currCount = 0;
        for (Event &event : m_events) {
            event.target = kNoDeadline;
        }
        RecalcSchedule();
    }

    /// @brief Registers an event. The returned ID must be used to refer to the event.
    /// @param[in] userID the user ID (for save states)
    /// @param[in] userContext the user context pointer
    /// @param[in] callback the event callback function
    /// @return the event ID for the newly registered event
    [[nodiscard]] EventID RegisterEvent(UserEventID userID, void *userContext, EventCallback callback) {
        assert(m_eventPtrs[userID] == kInvalidEvent);                    // ensure user IDs are unique
        assert(m_nextEventIndex <= std::numeric_limits<EventID>::max()); // IDtype value space exhausted
        EventID id = m_nextEventIndex;
        m_eventPtrs[userID] = id;
        Event &event = m_events[id];
        m_userIDs[id] = userID;
        event.userContext = userContext;
        event.callback = callback;
        event.countNumerator = 1;
        event.countDenominator = 1;
        ++m_nextEventIndex;
        return id;
    }

    /// @brief Replaces an event's callback function and user context pointer.
    /// @param[in] id the event ID to modify
    /// @param[in] userContext the user context pointer
    /// @param[in] callback the event callback function
    void SetEventCallback(EventID id, void *userContext, EventCallback callback) {
        Event &event = m_events[id];
        event.userContext = userContext;
        event.callback = callback;
    }

    /// @brief Sets the event cycle counting factor.
    ///
    /// This enables cycle counting between components of varying clock rates.
    /// @param[in] id the event ID to modify
    /// @param[in] numerator the cycle counting factor numerator
    /// @param[in] denominator the cycle counting factor denominator
    void SetEventCountFactor(EventID id, uint64 numerator, uint64 denominator) {
        assert(numerator > 0);
        assert(denominator > 0);
        Event &event = m_events[id];

        bool reschedule = false;
        if (event.target != kNoDeadline) {
            const uint64 oldTarget = event.target;
            const uint64 oldScaledCount = m_currCount * event.countNumerator / event.countDenominator;
            const sint64 remaining = oldTarget - oldScaledCount;
            const sint64 rescaledCount = m_currCount * numerator / denominator;

            event.target = rescaledCount + remaining;
            reschedule = remaining <= 0;
        }

        event.countNumerator = numerator;
        event.countDenominator = denominator;

        if (reschedule) {
            RecalcSchedule();
        }
    }

    /// @brief Retrieves the current value of the primary cycle counter.
    /// @return the current cycle count
    [[nodiscard]] FORCE_INLINE uint64 CurrentCount() const {
        return m_currCount;
    }

    /// @brief Retrieves the absolute cycle count of the earliest scheduled event.
    /// @return the cycle count of the next event to trigger
    [[nodiscard]] FORCE_INLINE uint64 NextCount() const {
        return m_nextCount;
    }

    /// @brief Retrieves a pointer to the absolute cycle count of the earliest scheduled event.
    /// @return a pointer to the cycle count of the next event to trigger
    [[nodiscard]] FORCE_INLINE const uint64 *NextCountPtr() const {
        return &m_nextCount;
    }

    /// @brief Retrieves the number of cycles remaining until the next event is triggered.
    ///
    /// If the result is negative, an event is late.
    /// @return the number of cycles until the next event
    [[nodiscard]] FORCE_INLINE sint64 RemainingCount() const {
        return static_cast<sint64>(m_nextCount) - static_cast<sint64>(m_currCount);
    }

    /// @brief Schedules the specified event to happen `interval` cycles from the current count.
    /// @param[in] id the event ID
    /// @param[in] interval the interval in cycles from the current cycle count
    FORCE_INLINE void ScheduleFromNow(EventID id, uint64 interval) {
        assert(id < kNumScheduledEvents);
        Event &event = m_events[id];
        const uint64 scaledCount = m_currCount * event.countNumerator / event.countDenominator;
        ScheduleEvent(id, scaledCount + interval);
    }

    /// @brief Schedules the specified event to happen at the specified cycle count.
    /// @param[in] id the event ID
    /// @param[in] target the absolute cycle count
    FORCE_INLINE void ScheduleAt(EventID id, uint64 target) {
        assert(id < kNumScheduledEvents);
        ScheduleEvent(id, target);
    }

    /// @brief Retrieves the scheduled target time for the event.
    /// @param[in] id the event ID.
    /// @return the absolute cycle count when the event is scheduled to trigger
    [[nodiscard]] FORCE_INLINE uint64 GetScheduleTarget(EventID id) const {
        assert(id < kNumScheduledEvents);
        const Event &event = m_events[id];
        return event.target;
    }

    /// @brief Retrieves the event's callback function pointer.
    /// @param[in] id the event ID.
    /// @return the function pointer assigned to the given event
    [[nodiscard]] FORCE_INLINE EventCallback GetEventCallback(EventID id) {
        assert(id < kNumScheduledEvents);
        const Event &event = m_events[id];
        return event.callback;
    }

    /// @brief Removes the specified event from the schedule.
    /// @param[in] id the event ID
    FORCE_INLINE void Cancel(EventID id) {
        assert(id < kNumScheduledEvents);
        Event &event = m_events[id];
        event.target = kNoDeadline;
    }

    /// @brief Checks if the specified event is scheduled to be triggered.
    /// @param[in] id the event ID
    /// @return `true` if the event was scheduled and has not yet triggered
    FORCE_INLINE bool IsScheduled(EventID id) const {
        assert(id < kNumScheduledEvents);
        const Event &event = m_events[id];
        if (event.target == kNoDeadline) {
            return false;
        }
        const uint64 scaledCurrCount = m_currCount * event.countNumerator / event.countDenominator;
        return scaledCurrCount < event.target;
    }

    /// @brief Advances the scheduler by the specified count and fire scheduled events.
    /// @param count the number of cycles to advance
    FORCE_INLINE void Advance(uint64 count) {
        m_currCount += count;
        if (m_currCount >= m_nextCount) {
            Execute();
        }
    }

    // -------------------------------------------------------------------------
    // Save states

    /// @brief Saves the Scheduler state into the given state object.
    ///
    /// This function should not be used directly. Use `ymir::Saturn::SaveState` with the full state object instead.
    ///
    /// @param[in] state the state object
    void SaveState(state::SchedulerState &state) const {
        state.currCount = m_currCount;
        for (size_t i = 0; i < kNumScheduledEvents; i++) {
            state.events[i].id = m_userIDs[i];
            state.events[i].target = m_events[i].target;
            state.events[i].countNumerator = m_events[i].countNumerator;
            state.events[i].countDenominator = m_events[i].countDenominator;
        }
    }

    /// @brief Validates the given state object.
    /// @param state the state object
    /// @return `true` if the state object is valid
    [[nodiscard]] bool ValidateState(const state::SchedulerState &state) const {
        for (size_t i = 0; i < kNumScheduledEvents; i++) {
            const size_t eventIndex = m_eventPtrs[state.events[i].id];
            if (eventIndex == kInvalidEvent) {
                return false;
            }
        }
        return true;
    }

    /// @brief Loads the Scheduler state from the given state object.
    ///
    /// This function should not be used directly. Use `ymir::Saturn::LoadState` with the full state object instead.
    ///
    /// This function does not validate the state.
    ///
    /// @param state the state object
    void LoadState(const state::SchedulerState &state) {
        m_currCount = state.currCount;
        for (size_t i = 0; i < kNumScheduledEvents; i++) {
            const size_t eventIndex = m_eventPtrs[state.events[i].id];
            assert(eventIndex != kInvalidEvent);
            m_events[eventIndex].target = state.events[i].target;
            m_events[eventIndex].countNumerator = state.events[i].countNumerator;
            m_events[eventIndex].countDenominator = state.events[i].countDenominator;
        }
        RecalcSchedule();
    }

private:
    /// @brief A cycle count representing the "not scheduled" state.
    static constexpr uint64 kNoDeadline = ~static_cast<uint64>(0);

    /// @brief A schedulable event.
    struct Event {
        uint64 target;           ///< Deadline in cycles relative to the component's clock
        uint64 countNumerator;   ///< Cycle scaling factor numerator
        uint64 countDenominator; ///< Cycle scaling factor denominator
        void *userContext;       ///< User context pointer
        EventCallback callback;  ///< Event callback function

        /// @brief Calculates the target cycle count scaled by the reciprocal of the scaling factor.
        /// @return `(target * countDenominator + countNumerator - 1) / countNumerator`
        [[nodiscard]] FORCE_INLINE uint64 CalcTargetScaledByReciprocal() const {
            return (target * countDenominator + countNumerator - 1) / countNumerator;
        }
    };

    /// @brief Schedules an event to execute at the specified time.
    /// @param[in] id the event ID
    /// @param[in] target the deadline in cycles
    FORCE_INLINE void ScheduleEvent(EventID id, uint64 target) {
        Event &event = m_events[id];
        event.target = target;
        const uint64 scaledTarget = event.CalcTargetScaledByReciprocal();
        if (scaledTarget < m_nextCount) {
            m_nextCount = scaledTarget;
            m_nextEvent = id;
        }
    }

    /// @brief Executes all scheduled events up to the current count.
    FORCE_INLINE void Execute() {
        while (m_currCount >= m_nextCount) {
            Event &event = m_events[m_nextEvent];
            assert(event.target != kNoDeadline);

            const uint64 currCount = m_currCount;

            uint64 target = event.target;
            const uint64 scaledCurrCount = currCount * event.countNumerator / event.countDenominator;
            if (scaledCurrCount >= target) {
                const EventCallback callback = event.callback;
                void *const userContext = event.userContext;
                EventContext eventContext;
                callback(eventContext, userContext);
                if (eventContext.reschedule) {
                    target += eventContext.interval;
                } else {
                    target = kNoDeadline;
                }
                event.target = target;
            }

            RecalcSchedule();
        }
    }

    /// @brief Recalculates the next deadline.
    FORCE_INLINE void RecalcSchedule() {
        m_nextCount = kNoDeadline;
        m_nextEvent = m_events.size();
        for (size_t index = 0; index < m_events.size(); ++index) {
            const Event &event = m_events[index];
            if (event.target == kNoDeadline) {
                continue;
            }
            const uint64 scaledTarget = event.CalcTargetScaledByReciprocal();
            if (scaledTarget < m_nextCount) {
                m_nextCount = scaledTarget;
                m_nextEvent = index;
            }
        }
    }

    uint64 m_currCount;                                     ///< The primary cycle counter
    uint64 m_nextCount;                                     ///< The cached cycle counter to the next event
    size_t m_nextEvent;                                     ///< The cached index of the next event
    std::array<Event, kNumScheduledEvents> m_events;        ///< Schedulable events
    std::array<UserEventID, kNumScheduledEvents> m_userIDs; ///< User IDs associated with events
    size_t m_nextEventIndex;                                ///< The next event index on which to register new events
    std::array<EventID, std::numeric_limits<UserEventID>::max() + 1> m_eventPtrs; ///< Translates user IDs to event IDs
};

} // namespace ymir::core
