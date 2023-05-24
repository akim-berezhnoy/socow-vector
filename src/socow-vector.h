#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <utility>

template <typename T, size_t SMALL_SIZE>
class socow_vector {
public:
  using value_type = T;

  using reference = T&;
  using const_reference = const T&;

  using pointer = T*;
  using const_pointer = const T*;

  using iterator = pointer;
  using const_iterator = const_pointer;

public:
  socow_vector() noexcept : _size(0), _is_small_object(true) {}

  explicit socow_vector(size_t capacity) : socow_vector(socow_vector(), capacity) {}

  socow_vector(const socow_vector& other) : socow_vector() {
    operator=(other);
  }

  socow_vector& operator=(const socow_vector& other) {
    if (this == &other) {
      return *this;
    }
    size_t min_size = std::min(size(), other.size());
    size_t max_size = std::max(size(), other.size());
    if (other._is_small_object) {
      if (_is_small_object) {
        socow_vector tmp;
        std::uninitialized_copy_n(other._static_buffer, min_size, tmp._static_buffer);
        tmp._size = min_size;
        std::uninitialized_copy_n(other._static_buffer + min_size, other.size() - min_size, _static_buffer + min_size);
        _size = max_size;
        std::swap_ranges(_static_buffer, _static_buffer + min_size, tmp._static_buffer);
        destroy_last_n(max_size - other.size());
      } else {
        strong_copy_to_big_which_will_become_small(other._static_buffer, other.size(), *this);
      }
    } else {
      this->~socow_vector();
      new (this) socow_vector();
      _heap_buffer = other._heap_buffer;
      _heap_buffer->add_ref();
    }
    _is_small_object = other._is_small_object;
    _size = other.size();
    return *this;
  }

  void swap(socow_vector& other) {
    size_t max_size = std::max(size(), other.size()), same_range = size() + other.size() - max_size;
    if (_is_small_object && other._is_small_object) {
      bool bigger = size() > other.size();
      std::uninitialized_copy_n((bigger ? _static_buffer : other._static_buffer) + same_range, max_size - same_range,
                                (bigger ? other._static_buffer : _static_buffer) + same_range);
      (size() > other.size() ? *this : other).destroy_last_n(max_size - same_range);
      std::swap(_is_small_object, other._is_small_object);
      std::swap(_size, other._size);
      std::swap_ranges(_static_buffer, _static_buffer + same_range, other._static_buffer);
    } else {
      socow_vector& static_or_dynamic_vector = _is_small_object ? *this : other;
      socow_vector& dynamic_vector = _is_small_object ? other : *this;
      socow_vector tmp = dynamic_vector;
      dynamic_vector = static_or_dynamic_vector;
      static_or_dynamic_vector = tmp;
    }
  }

  ~socow_vector() noexcept {
    if (_is_small_object) {
      destroy_last_n(size());
    } else {
      release_ref();
      _is_small_object = true;
    }
  }

  reference operator[](size_t index) {
    assert(index < size());
    return data()[index];
  }

  const_reference operator[](size_t index) const noexcept {
    assert(index < size());
    return *(cbegin() + index);
  }

  pointer data() {
    if (_is_small_object) {
      return _static_buffer;
    } else {
      ensure_unique();
      return _heap_buffer->flex;
    }
  }

  const_pointer data() const noexcept {
    return _is_small_object ? _static_buffer : _heap_buffer->flex;
  }

  size_t size() const noexcept {
    return _size;
  }

  reference front() {
    assert(size() > 0);
    return operator[](0);
  }

  const_reference front() const noexcept {
    assert(size() > 0);
    return operator[](0);
  }

  reference back() {
    assert(size() > 0);
    return operator[](size() - 1);
  }

  const_reference back() const noexcept {
    assert(size() > 0);
    return operator[](size() - 1);
  }

  void push_back(const T& value) {
    if (size() == capacity() || is_shared()) {
      socow_vector tmp(*this, size() == 0 ? 1 : size() * 2);
      tmp.push_back(value);
      operator=(tmp);
    } else {
      new (data() + size()) value_type(value);
      ++_size;
    }
  }

  void pop_back() {
    assert(size() > 0);
    erase((_is_small_object ? _static_buffer : _heap_buffer->flex) + size() - 1);
  }

  bool empty() const noexcept {
    return !size();
  }

  size_t capacity() const noexcept {
    return _is_small_object ? SMALL_SIZE : _heap_buffer->capacity;
  }

  void reserve(size_t new_capacity) {
    if (new_capacity > capacity() || (is_shared() && new_capacity >= size())) {
      operator=(socow_vector(*this, new_capacity));
    }
  }

  void shrink_to_fit() {
    if (size() == capacity() || capacity() == SMALL_SIZE) {
      return;
    }
    if (size() > SMALL_SIZE) {
      operator=(socow_vector(*this, size()));
    } else {
      shrink_big_to_small(size());
    }
  }

  void clear() noexcept {
    if (_is_small_object || !_heap_buffer->ref_count) {
      destroy_last_n(size());
    } else {
      --_heap_buffer->ref_count;
      _is_small_object = true;
    }
    _size = 0;
  }

  iterator begin() {
    return data();
  }

  iterator end() {
    return data() + size();
  }

  const_iterator begin() const noexcept {
    return data();
  }

  const_iterator end() const noexcept {
    return data() + size();
  }

  const_iterator cbegin() const noexcept {
    return begin();
  }

  const_iterator cend() const noexcept {
    return end();
  }

  iterator insert(const_iterator pos, const T& value) {
    ptrdiff_t index = pos - cbegin();
    if (size() == capacity() || is_shared()) {
      socow_vector tmp(size() == 0 ? 1 : size() * 2);
      std::uninitialized_copy_n(cbegin(), index, tmp.begin());
      tmp._size = index;
      new (tmp.begin() + index) value_type(value);
      tmp._size += 1;
      std::uninitialized_copy_n(cbegin() + index, size() - index, tmp.begin() + index + 1);
      tmp._size = size() + 1;
      operator=(tmp);
      return _heap_buffer->flex + index;
    } else {
      push_back(value);
      if (size() == 1) {
        return begin();
      }
      std::rotate(begin() + index, end() - 1, end());
      return begin() + index;
    }
  }

  iterator erase(const_iterator pos) {
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last) {
    size_t range = last - first, index = first - cbegin();
    if (first == last) {
      return data() + index;
    }
    if (is_shared()) {
      if (size() == SMALL_SIZE + 1) {
        shrink_big_to_small(SMALL_SIZE);
        return end();
      } else if (size() - range > SMALL_SIZE) {
        socow_vector tmp(size() - range);
        std::uninitialized_copy(last, cend(), std::uninitialized_copy(cbegin(), first, tmp.begin()));
        tmp._size = size() - range;
        operator=(tmp);
        return _heap_buffer->flex + index;
      } else {
        socow_vector tmp = *this;
        try {
          iterator second_batch_start = std::uninitialized_copy(tmp.cbegin(), first, _static_buffer);
          try {
            std::uninitialized_copy(last, tmp.cend(), second_batch_start);
          } catch (...) {
            for (size_t i = 0; i < index; ++i) {
              _static_buffer[i].~value_type();
            }
            throw;
          }
        } catch (...) {
          _heap_buffer = tmp._heap_buffer;
          throw;
        }
        tmp.release_ref();
        _size -= range;
        _is_small_object = true;
        return _static_buffer + index;
      }
    } else {
      for (size_t i = index; i < size() - range; ++i) {
        std::swap(operator[](i), operator[](i + range));
      }
      destroy_last_n(range);
      _size -= range;
      return begin() + index;
    }
  }

private:
  socow_vector(const socow_vector& other, size_t capacity) : _size(0), _is_small_object(capacity <= SMALL_SIZE) {
    size_t size_to_copy = std::min(capacity, other.size());
    if (!_is_small_object) {
      _heap_buffer = static_cast<dynamic_buffer*>(operator new(sizeof(dynamic_buffer) + sizeof(value_type) * capacity));
      new (_heap_buffer) dynamic_buffer{capacity};
    }
    std::uninitialized_copy_n(other.cbegin(), size_to_copy, begin());
    _size = size_to_copy;
  }

  void release_ref() noexcept {
    if (_heap_buffer->ref_count == 0) {
      destroy_last_n(size());
      operator delete(_heap_buffer);
    } else {
      _heap_buffer->ref_count--;
    }
  }

  static void strong_copy_to_big_which_will_become_small(const_pointer from, size_t n, socow_vector& to) {
    socow_vector tmp = to;
    try {
      std::uninitialized_copy_n(from, n, to._static_buffer);
    } catch (...) {
      to._heap_buffer = tmp._heap_buffer;
      throw;
    }
    tmp.release_ref();
  }

  void shrink_big_to_small(size_t new_size) {
    strong_copy_to_big_which_will_become_small(this->_heap_buffer->flex, new_size, *this);
    _size = new_size;
    _is_small_object = true;
  }

  void ensure_unique() {
    if (_is_small_object) {
      return;
    }
    if (_heap_buffer->ref_count) {
      operator=(socow_vector(*this, capacity()));
    }
  }

  void destroy_last_n(size_t n) noexcept {
    for (size_t i = 1; i <= n; ++i) {
      operator[](size() - i).~value_type();
    }
  }

  bool is_shared() {
    return !_is_small_object && _heap_buffer->ref_count;
  }

  struct dynamic_buffer {
    dynamic_buffer(size_t capacity) : capacity(capacity), ref_count(0) {}

    void add_ref() {
      ++ref_count;
    }

    size_t capacity;
    size_t ref_count;
    value_type flex[0];
  };

  union {
    value_type _static_buffer[SMALL_SIZE];
    dynamic_buffer* _heap_buffer{nullptr};
  };

private:
  size_t _size;
  bool _is_small_object;
};
