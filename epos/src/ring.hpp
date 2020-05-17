#pragma once
#include <array>
#include <cstdlib>

template <typename T, std::size_t S>
class ring {
public:
  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::size_t;
    using value_type = T;
    using reference = value_type &;
    using pointer = value_type *;

    constexpr iterator() noexcept = default;
    constexpr iterator(value_type* ptr, std::size_t pos) noexcept : ptr_(ptr), pos_(pos) {}

    constexpr bool operator==(const iterator& other) const noexcept {
      return pos_ == other.pos_;
    }

    constexpr bool operator!=(const iterator& other) const noexcept {
      return !(*this == other);
    }

    constexpr iterator& operator++() noexcept {
      if (++pos_ >= S + 1) {
        pos_ = 0;
      }
      return *this;
    }

    iterator operator++(int) = delete;

    constexpr reference operator*() const noexcept {
      return *operator->();
    }

    constexpr pointer operator->() const noexcept {
      return ptr_ + pos_;
    }

  private:
    value_type* ptr_ = nullptr;
    std::size_t pos_ = 0;
  };

  ring() = default;

  constexpr iterator begin() noexcept {
    return { arr_.data(), (pos_ == S - 1 ? 0 : (pos_ == S ? 1 : pos_ + 2)) };
  }

  constexpr iterator end() noexcept {
    return { arr_.data(), (pos_ == S ? 0 : pos_ + 1) };
  }

  constexpr T& advance() noexcept {
    if (++pos_ >= S + 1) {
      pos_ = 0;
    }
    return arr_[pos_];
  }

private:
  std::array<T, S + 1> arr_ = {};
  std::size_t pos_ = 0;
};