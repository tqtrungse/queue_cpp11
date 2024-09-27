//
// Created by Trung Tran <tqtrungse@gmail.com> on 1/12/2024.
//

#ifndef QUEUE_HPP
#define QUEUE_HPP

#include <atomic>
#include <cassert>
#include <memory>
#include <tuple>
#include <type_traits>

#include "cache_padded.h"

namespace t2 {
static const uint16_t kMask16 = 1 << 15;
static const uint32_t kMask32 = 1 << 31;

enum class State : int8_t {
    SUCCESS = 0,
    EMPTY = -1,
    FULL = -2,
    CLOSED = -3,
};

template <typename T>
class queue
{
private:
    struct elem
    {
        // current lap,
        // the element is ready for writing on laps 0, 2, 4, ...
        // for reading on laps 1, 3, 5, ...
        std::atomic<uint16_t> lap{ 0 };

        // User data
        T value;

        elem() noexcept = default;
    };

    // queue capacity
    uint16_t cap_;

    // ring buffer
    std::unique_ptr<elem[]> buf_{};

    // calculate length.
    std::atomic<uint32_t> size_{ 0 };

    // send and receive positions,
    // low 16 bits represent position in the buffer,
    // high 16 bits represent the current “lap” over the ring buffer
    alignas(CACHE_PADDED) std::atomic<uint32_t> sendX_{ 0 };
    alignas(CACHE_PADDED) uint32_t recvX_{ static_cast<uint32_t>(1 << 16) };

    std::tuple<elem *, uint16_t, State> select_4_read()
    {
        auto pos{ (uint16_t)recvX_ };
        auto lap{ (uint16_t)(recvX_ >> 16) };
        auto elem = &buf_.get()[pos];
        auto elem_lap{ elem->lap.load(std::memory_order_acquire) };

        if (lap == elem_lap) {
            // The element is ready for writing on this lap.
            // Try to claim the right to write to this element.
            if (pos + 1 < cap_) {
                recvX_ = recvX_ + 1;
            } else {
                recvX_ = (uint32_t)(lap + 2) << 16;
            }
            return std::make_tuple(elem, elem_lap, State::SUCCESS);
        } else if ((int16_t)(lap - elem_lap) > 0) {
            // The element is not yet read on the previous lap,
            // the chan is empty.
            if (lap > elem->lap.load(std::memory_order_acquire)) {
                return std::make_tuple(nullptr, 0, State::EMPTY);
            }
            // The element has already been written on this lap,
            // this means that `send_x` has been changed as well,
            // retry.
        }
        // The case lap < elem_lap occurs if and only if environment have more than 2 threads
        // and more than 2 disputing threads are same read or write operation.
        return std::make_tuple(nullptr, 0, State::EMPTY); // Fix lint.
    }

    std::tuple<elem *, uint16_t, State> select_4_write()
    {
        uint16_t pos;
        uint16_t lap;
        uint16_t elem_lap;
        uint32_t x;
        elem *elem;

        x = sendX_.load(std::memory_order_relaxed);
        while (true) {
            lap = (x >> 16);
            if (lap >= kMask16) {
                return std::make_tuple(nullptr, 0, State::CLOSED);
            }

            pos = (uint16_t)x;
            elem = &buf_.get()[pos];
            elem_lap = elem->lap.load(std::memory_order_acquire);

            if (lap == elem_lap) {
                // The element is ready for writing on this lap.
                // Try to claim the right to write to this element.
                uint32_t new_x;
                if (pos + 1 < cap_) {
                    new_x = x + 1;
                } else {
                    new_x = (uint32_t)(lap + 2) << 16;
                }

                auto m1{ std::memory_order_acquire };
                auto m2{ std::memory_order_relaxed };
                if (sendX_.compare_exchange_weak(x, new_x, m1, m2)) {
                    // We own the element.
                    return std::make_tuple(elem, elem_lap, State::SUCCESS);
                }
            } else if ((int16_t)(lap - elem_lap) > 0) {
                // The element is not yet write on the previous lap,
                // the chan is full.
                if (lap > elem->lap.load(std::memory_order_acquire)) {
                    return std::make_tuple(nullptr, 0, State::FULL);
                }
                // The element has already been read on this lap,
                // this means that `recv_x` has been changed as well,
                // retry.
            }
            // The case lap < elem_lap occurs if and only if environment have more than 2 threads
            // and more than 2 disputing threads are same read or write operation.
        }
    }

public:
    explicit queue(uint16_t cap) noexcept : cap_{ cap }
    {
        static_assert(std::is_copy_assignable<T>::value || std::is_move_assignable<T>::value,
                      "T have to copy or move assigment for push");
        static_assert(std::is_default_constructible<T>::value
                              && std::is_move_constructible<T>::value,
                      "T have to default and move constructor for pop");
        // For buf
        assert(cap > 0);
        buf_.reset(new elem[cap]);
    }

    State try_push(const T &val)
    {
        elem *elem;
        uint16_t elem_lap;
        State state;

        std::tie(elem, elem_lap, state) = this->select_4_write();
        if (state == State::SUCCESS) {
            elem->value = val;
            elem->lap.store(elem_lap + 1, std::memory_order_release);
            size_.fetch_add(1, std::memory_order_relaxed);
        }
        return state;
    }

    State try_push(T &&val)
    {
        elem *elem;
        uint16_t elem_lap;
        State state;

        std::tie(elem, elem_lap, state) = this->select_4_write();
        if (state == State::SUCCESS) {
            elem->value = std::move(val);
            elem->lap.store(elem_lap + 1, std::memory_order_release);
            size_.fetch_add(1, std::memory_order_relaxed);
        }
        return state;
    }

    std::tuple<T, State> try_pop()
    {
        elem *elem;
        uint16_t elem_lap;
        State state;

        std::tie(elem, elem_lap, state) = this->select_4_read();
        if (state == State::SUCCESS) {
            T out{ std::move(elem->value) };
            elem->lap.store(elem_lap + 1, std::memory_order_release);
            size_.fetch_add(-1, std::memory_order_relaxed);
            return std::make_tuple(std::move(out), state);
        }
        return std::make_tuple(T{}, state);
    }

    T *try_peek()
    {
        elem *elem;
        State state;

        std::tie(elem, std::ignore, state) = this->select_4_read();
        if (state == State::SUCCESS) {
            return &elem->value;
        }
        return nullptr;
    }

    void close()
    {
        auto x{ sendX_.load(std::memory_order_acquire) };
        auto new_x{ x | kMask32 };
        auto m1{ std::memory_order_acquire };
        auto m2{ std::memory_order_relaxed };

        while (!sendX_.compare_exchange_weak(x, new_x, m1, m2)) {
            delay(10);
        }
    }

    uint32_t len() const noexcept { return size_.load(std::memory_order_relaxed); }

    bool is_close() const noexcept
    {
        return (sendX_.load(std::memory_order_relaxed) & kMask32) != 0;
    }
};
} // namespace t2

#endif // QUEUE_HPP
