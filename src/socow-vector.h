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
    size_t min_size = std::min(_size, other._size);
    size_t max_size = std::max(_size, other._size);
    if (other._is_small_object) {
      if (_is_small_object) {
        socow_vector tmp;
        std::uninitialized_copy_n(other._static_buffer, min_size, tmp._static_buffer);
        tmp._size = min_size;
        std::uninitialized_copy_n(other._static_buffer + min_size, other._size - min_size, _static_buffer + min_size);
        _size = max_size;
        std::swap_ranges(_static_buffer, _static_buffer + min_size, tmp._static_buffer);
        clear(max_size - other._size);
      } else {
        socow_vector tmp(*this);
        strong_copy(*this, other);
        tmp.release_ref();
      }
    } else {
      this->~socow_vector();
      _heap_buffer = other._heap_buffer;
      add_ref();
    }
    _is_small_object = other._is_small_object;
    _size = other._size;
    return *this;
  }

  void swap(socow_vector& other) {
    size_t max_size = std::max(_size, other._size);
    size_t same_range = std::min(_size, other._size);
    size_t diff = max_size - same_range;
    if (other._is_small_object) {
      if (_is_small_object) {
        bool bigger = _size > other._size;
        std::uninitialized_copy_n((bigger ? _static_buffer : other._static_buffer) + same_range, max_size - same_range,
                                  (bigger ? other._static_buffer : _static_buffer) + same_range);
        (_size > other._size ? *this : other).clear(diff);
      } else {
        dynamic_buffer* tmp = _heap_buffer;
        strong_copy(*this, other);
        other.clear(other._size);
        other._heap_buffer = tmp;
      }
    } else {
      if (_is_small_object) {
        dynamic_buffer* tmp = other._heap_buffer;
        strong_copy(other, *this);
        clear(_size);
        _heap_buffer = tmp;
      } else {
        std::swap(_heap_buffer, other._heap_buffer);
      }
    }
    std::swap(_size, other._size);
    std::swap(_is_small_object, other._is_small_object);
    if (_is_small_object && other._is_small_object) {
      std::swap_ranges(_static_buffer, _static_buffer + same_range, other._static_buffer);
    }
  }

  ~socow_vector() noexcept {
    _is_small_object ? clear(_size) : release_ref();
  }

  reference operator[](size_t index) {
    assert(index < _size);
    return data()[index];
  }

  const_reference operator[](size_t index) const noexcept {
    assert(index < _size);
    return element(index);
  }

  pointer data() {
    return _is_small_object ? _static_buffer : (ensure_unique(), _heap_buffer->flex);
  }

  const_pointer data() const noexcept {
    return _is_small_object ? _static_buffer : _heap_buffer->flex;
  }

  size_t size() const noexcept {
    return _size;
  }

  reference front() noexcept {
    return operator[](0);
  }

  const_reference front() const noexcept {
    return element(0);
  }

  reference back() noexcept {
    assert(_size > 0);
    return operator[](_size - 1);
  }

  const_reference back() const noexcept {
    assert(_size > 0);
    return element(_size - 1);
  }

  void push_back(const T& value) {
    if (_size == capacity()) {
      socow_vector tmp(*this, _size == 0 ? 1 : _size * 2);
      tmp.push_back(value);
      *this = tmp;
    } else {
      new (data() + _size) value_type(value);
      ++_size;
    }
  }

  void pop_back() {
    assert(_size > 0);
    ensure_unique();
    --_size;
    element(_size).~value_type();
  }

  bool empty() const noexcept {
    return _size == 0;
  }

  size_t capacity() const noexcept {
    return _is_small_object ? SMALL_SIZE : _heap_buffer->capacity;
  }

  void reserve(size_t new_capacity) {
    if (new_capacity > capacity() || (!_is_small_object && new_capacity >= _size && _heap_buffer->ref_count)) {
      *this = socow_vector(*this, new_capacity);
    }
  }

  void shrink_to_fit() {
    if (_size == capacity() || capacity() == SMALL_SIZE) {
      return;
    }
    *this = socow_vector(*this, _size);
    if (_size <= SMALL_SIZE) {
      _is_small_object = true;
    }
  }

  void clear() {
    if (_is_small_object) {
      clear(_size);
    } else {
      if (_heap_buffer->ref_count) {
        release_ref();
        _is_small_object = true;
      } else {
        clear(_size);
      }
    }
    _size = 0;
  }

  iterator begin() {
    return data();
  }

  iterator end() {
    return data() + _size;
  }

  const_iterator begin() const noexcept {
    return data();
  }

  const_iterator end() const noexcept {
    return data() + _size;
  }

  iterator insert(const_iterator pos, const T& value) {
    ptrdiff_t index = pos - std::as_const(*this).begin();
    if (_size == capacity()) {
      socow_vector tmp(_size + 1);
      std::uninitialized_copy_n(_is_small_object ? _static_buffer : _heap_buffer->flex, index, tmp._heap_buffer->flex);
      tmp._size = index;
      new (tmp._heap_buffer->flex + index) value_type(value);
      tmp._size += 1;
      std::uninitialized_copy_n((_is_small_object ? _static_buffer : _heap_buffer->flex) + index, _size - index,
                                tmp._heap_buffer->flex + index + 1);
      tmp._size = _size + 1;
      operator=(tmp);
      return _heap_buffer->flex + index;
    }
    push_back(value);
    if (size() == 1) {
      return begin();
    }
    std::swap(element(index), element(_size - 1));
    for (size_t i = 2; i < _size - index; ++i) {
      std::swap(element(_size - i), element(_size - i + 1));
    }
    return begin() + index;
  }

  iterator erase(const_iterator pos) {
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last) {
    size_t range = last - first, index = first - std::as_const(*this).begin();
    if (first == last) {
      return data() + index;
    }
    ensure_unique();
    const socow_vector& const_this = std::as_const(*this);
    for (size_t i = index; i < (const_this.end() - const_this.begin()) - range; ++i) {
      std::swap(element(i), element(i + range));
    }
    for (size_t i = 0; i < range; ++i) {
      pop_back();
    }
    return begin() + index;
  }

private:
  socow_vector(const socow_vector& other, size_t capacity) : _is_small_object(capacity <= SMALL_SIZE) {
    if (capacity > SMALL_SIZE) {
      _heap_buffer = static_cast<dynamic_buffer*>(operator new(sizeof(dynamic_buffer) + sizeof(value_type) * capacity));
      try {
        new (_heap_buffer) dynamic_buffer{capacity, 0};
        std::uninitialized_copy_n(other._is_small_object ? other._static_buffer : other._heap_buffer->flex, other._size,
                                  _heap_buffer->flex);
      } catch (...) {
        operator delete(_heap_buffer);
        _heap_buffer = nullptr;
        throw;
      }
    } else {
      std::uninitialized_copy_n(other._is_small_object ? other._static_buffer : other._heap_buffer->flex, other._size,
                                _static_buffer);
    }
    _size = other._size;
  }

  void add_ref() {
    assert(_heap_buffer);
    ++_heap_buffer->ref_count;
  }

  void release_ref() {
    if (!_heap_buffer) {
      return;
    }
    if (_heap_buffer->ref_count == 0) {
      clear(_size);
      operator delete(_heap_buffer);
      _heap_buffer = nullptr;
    } else {
      _heap_buffer->ref_count--;
    }
  }

  void strong_copy(socow_vector& to, const socow_vector& from) {
    dynamic_buffer* tmp = to._heap_buffer;
    try {
      std::uninitialized_copy_n(from._static_buffer, from._size, to._static_buffer);
    } catch (...) {
      to._heap_buffer = tmp;
      throw;
    }
  }

  void ensure_unique() {
    if (_is_small_object) {
      return;
    }
    assert(_heap_buffer);
    if (_heap_buffer->ref_count) {
      *this = socow_vector(*this, capacity());
    }
  }

  reference element(size_t index) {
    return _is_small_object ? _static_buffer[index] : _heap_buffer->flex[index];
  }

  const_reference element(size_t index) const {
    return _is_small_object ? _static_buffer[index] : _heap_buffer->flex[index];
  }

  void clear(size_t n) noexcept {
    for (size_t i = 1; i <= n; ++i) {
      element(_size - i).~value_type();
    }
  }

  struct dynamic_buffer {
    dynamic_buffer() = default;

    dynamic_buffer(size_t capacity, size_t refs) : capacity(capacity), ref_count(refs) {}

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
