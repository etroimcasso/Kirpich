#pragma once

// A fixed-capacity inline vector: up to N elements stored inline (no heap), with a running count of
// how many are live. Built for compile-time data that varies in length up to a known maximum - a
// composed sprite holds between one and kMaxSpriteParts parts, for instance - where a plain
// std::array would force every row to the maximum and std::vector would forbid constexpr use.
//
// Construct one from a braced list; the element count must not exceed N (a longer list is a compile
// error in a constant expression, a debug assert at run time). Iterate the live prefix with a
// range-for, index it with operator[], compare two with ==. Trailing unused slots are value-
// initialized, so equality reflects only the live elements.

#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>

namespace kirpich {

template <typename T, std::size_t N>
class BoundedVec {
public:
    constexpr BoundedVec() = default;

    constexpr BoundedVec(std::initializer_list<T> init) {
        assert(init.size() <= N && "BoundedVec initializer exceeds capacity");
        for (const T& value : init) {
            data_[size_++] = value;
        }
    }

    [[nodiscard]] constexpr std::size_t size() const { return size_; }
    [[nodiscard]] constexpr bool empty() const { return size_ == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() { return N; }

    [[nodiscard]] constexpr const T& operator[](std::size_t i) const {
        assert(i < size_ && "BoundedVec index out of range");
        return data_[i];
    }

    [[nodiscard]] constexpr const T* begin() const { return data_.data(); }
    [[nodiscard]] constexpr const T* end() const { return data_.data() + size_; }

    friend constexpr bool operator==(const BoundedVec&, const BoundedVec&) = default;

private:
    std::array<T, N> data_{};
    std::size_t size_ = 0;
};

}  // namespace kirpich
