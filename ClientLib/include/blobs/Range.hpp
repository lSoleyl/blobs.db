#pragma once


namespace blobs {


/** A templated range object, which owns an array, is iterable and movable.
 */
template<typename T>
class Range {
public:
  Range() : data(nullptr), n(0) {}
  Range(T* data, size_t n) : data(data), n(n) {}
  ~Range() {
    if (data) {
      delete[] data;
    }
  }

  Range(Range&& other) : data(other.data), n(other.n) {
    other.data = nullptr;
    other.n = 0;
  }

  Range& operator=(Range&& other) {
    this->~Range();
    new (this) Range(std::move(other));
    return *this;
  }

  using iterator = T*;
  using const_iterator = const T*;
  
  iterator begin() { return data; }
  iterator end() { return data + n; }
  const_iterator begin() const { return data; }
  const_iterator end() const { return data + n; }

  size_t size() const { return n; }

private:
  // Range is not copyable
  Range(const Range&) = delete;
  Range& operator=(const Range&) = delete;

  T* data;
  size_t n;
};



}
