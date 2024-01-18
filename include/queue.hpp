//
// Created by Trung Tran <tqtrungse@gmail.com> on 1/12/2024.
//

#ifndef QUEUE_HPP
#define QUEUE_HPP

#include <tuple>
#include <atomic>
#include <memory>
#include <cassert>
#include <type_traits>

#include "cache_padded.h"

namespace t2 {
    template<typename T>
    class queue {
    private:
        struct elem {
            // current lap,
            // the element is ready for writing on laps 0, 2, 4, ...
            // for reading -- on laps 1, 3, 5, ...
            std::atomic<uint16_t> lap{0};

            // User data
            T value;

            elem() noexcept = default;
        };

        // queue capacity
        uint16_t cap;

        // ring buffer
        std::unique_ptr<elem> buf{};

        // calculate length.
        std::atomic<int32_t> size{0};

        // send and receive positions,
        // low 16 bits represent position in the buffer,
        // high 16 bits represent the current “lap” over the ring buffer
        alignas(CACHE_PADDED) uint32_t send_x{0};
        alignas(CACHE_PADDED) uint32_t recv_x{static_cast<uint32_t>(1 << 16)};


        std::tuple<elem*, uint16_t, bool> select(uint32_t& X) {
            uint16_t pos;
            uint16_t lap;
            uint16_t elem_lap;
            uint32_t x;
            uint32_t new_x;
            elem* elem;

            while (true) {
                x = X;
                pos = (uint16_t) x;
                lap = (uint16_t) (x >> 16);
                elem = &this->buf.get()[pos];
                elem_lap = elem->lap.load(std::memory_order_acquire);

                if (lap == elem_lap) {
                    // The element is ready for writing on this lap.
                    // Try to claim the right to write to this element.
                    if (pos + 1 < this->cap) {
                        new_x = x + 1;
                    } else {
                        new_x = (uint32_t) (lap + 2) << 16;
                    }

                    X = new_x;
                    // We own the element, do non-atomic write.
                    return std::make_tuple(elem, elem_lap, true);
                } else if ((int16_t) (lap - elem_lap) > 0) {
                    // The element is not yet write/read on the previous lap,
                    // the chan is empty/full.
                    if (lap > elem->lap.load(std::memory_order_acquire)) {
                        return std::make_tuple(nullptr, 0, false);
                    }
                    // The element has already been written/read on this lap,
                    // this means that `send_x`/`recv_x` has been changed as well,
                    // retry.
                } else {
                    // The element has already been written on this lap,
                    // this means that send_x has been changed as well,
                    // retry.
                }
            }
        }

    public:
        explicit queue(uint16_t Cap) noexcept
                : cap{Cap} {
            static_assert(
                    std::is_copy_assignable<T>::value || std::is_move_assignable<T>::value,
                    "T have to copy or move assigment for push"
            );
            static_assert(
                    std::is_default_constructible<T>::value && std::is_move_constructible<T>::value,
                    "T have to default and move constructor for pop"
            );
            // For buf
            assert(Cap > 0);
            this->buf.reset(new elem[Cap]);
        }

        bool try_push(const T& val) {
            elem* elem;
            uint16_t elem_lap;
            bool success;

            std::tie(elem, elem_lap, success) = this->select(this->send_x);
            if (!success) {
                return false;
            }
            elem->value = val;
            elem->lap.store(elem_lap + 1, std::memory_order_release);
            this->size.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        bool try_push(T&& val) {
            elem* elem;
            uint16_t elem_lap;
            bool success;

            std::tie(elem, elem_lap, success) = this->select(this->send_x);
            if (!success) {
                return false;
            }
            elem->value = std::move(val);
            elem->lap.store(elem_lap + 1, std::memory_order_release);
            this->size.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        T try_pop() {
            elem* elem;
            uint16_t elem_lap;
            bool success;

            std::tie(elem, elem_lap, success) = this->select(this->recv_x);
            if (!success) {
                return T{};
            }
            T out{std::move(elem->value)};
            elem->lap.store(elem_lap + 1, std::memory_order_release);
            this->size.fetch_add(-1, std::memory_order_relaxed);
            return std::move(out);
        }

        T* try_peek() {
            elem* elem;
            bool success;

            std::tie(elem, std::ignore, success) = this->select(this->recv_x);
            if (!success) {
                return nullptr;
            }
            return &elem->value;
        }

        int32_t len() const noexcept {
            return this->size.load(std::memory_order_acquire);
        }
    };
}

#endif // QUEUE_HPP
