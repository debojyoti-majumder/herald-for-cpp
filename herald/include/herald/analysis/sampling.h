//  Copyright 2021 Herald Project Contributors
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef SAMPLING_H
#define SAMPLING_H

#include <array>
#include <cstdint>

#include "../datatype/date.h"

namespace herald {
namespace analysis {
/// A set of structs compatible with, but not reliant upon, views and ranges in Herald
namespace sampling {

using namespace herald::datatype;

template <typename ValT>
struct Sample {
  using value_type = ValT;

  Date taken; // Date first for alignment reasons
  ValT value;

  Sample() : taken(), value() {} // default ctor (required for array)
  Sample(Date sampled, ValT v) : taken(Date{sampled.secondsSinceUnixEpoch()}), value(v) {}
  Sample(const Sample& other) : taken(Date{other.taken.secondsSinceUnixEpoch()}), value(other.value) {} // copy ctor
  Sample(Sample&& other) : taken(std::move(other.taken)), value(std::move(other.value)) {} // move ctor
  ~Sample() = default;

  Sample& operator=(Sample&& other) {
    taken = std::move(other.taken);
    value = std::move(other.value);
    return *this;
  }

  Sample& operator=(const Sample& other) {
    taken = Date{other.taken.secondsSinceUnixEpoch()};
    value = other.value;
    return *this;
  }

  // Note ValT MUST have comparison operators OR conversion to double defined

  // template <typename T>
  // bool operator>(const T& other) const {
  //   return value > other;
  // }

  // template <typename T>
  // bool operator>=(const T& other) const {
  //   return value >= other;
  // }

  // template <typename T>
  // bool operator<(const T& other) const {
  //   return value < other;
  // }

  // template <typename T>
  // bool operator<=(const T& other) const {
  //   return value <= other;
  // }

  // template <typename T>
  // bool operator==(const T& other) const {
  //   return value == other;
  // }

  // template <typename T>
  // bool operator!=(const T& other) const {
  //   return value != other;
  // }

  operator double() const noexcept {
    return (double)value;
  }
};

/// FWD DECLARATION
template <typename SampleListT,
          typename ValT = typename SampleListT::value_type>
struct SampleIterator;

/// A Circular container for Samples
/// Can be used as a container in the views library
template <typename SampleT, // This is Sample<SampleValueT>
          std::size_t MaxSize,
          typename SampleValueT = typename std::remove_cv<typename SampleT::value_type>::type
         >
struct SampleList {
  using value_type = SampleT; // MUST be before the next line!
  using difference_type = std::size_t;
  using iterator = SampleIterator<SampleList<SampleT,MaxSize>>;
  using size_type = std::size_t;

  static constexpr std::size_t max_size = MaxSize;

  SampleList() : data(), oldestPosition(SIZE_MAX), newestPosition(SIZE_MAX) {};
  SampleList(const SampleList&) = delete; // no shallow copies allowed
  ~SampleList() = default;

  void push(Date taken, SampleValueT val) {
    if (SIZE_MAX == newestPosition) {
      newestPosition = 0;
      oldestPosition = 0;
    } else {
      if (newestPosition == (oldestPosition - 1)) {
        ++oldestPosition;
        if (oldestPosition == data.size()) {
          oldestPosition = 0;
        }
      }
      ++newestPosition;
    }
    if (newestPosition == data.size()) {
      // just gone past the end of the container
      newestPosition = 0;
      if (0 == oldestPosition) {
        ++oldestPosition; // erases oldest if not already removed
      }
    }
    data[newestPosition] = SampleT{taken,val};
  }

  std::size_t size() const {
    if (newestPosition == SIZE_MAX) return 0;
    if (newestPosition >= oldestPosition) {
      // not overlapping the end
      return newestPosition - oldestPosition + 1;
    }
    // we've overlapped
    return (1 + newestPosition) + (data.size() - oldestPosition);
  }

  const SampleT& operator[](std::size_t idx) const {
    if (newestPosition >= oldestPosition) {
      return data[idx + oldestPosition];
    }
    if (idx + oldestPosition >= data.size()) {
      // TODO handle the situation where this pos > newestPosition (i.e. gap in the middle)
      return data[idx + oldestPosition - data.size()];
    }
    return data[idx + oldestPosition];
  }

  void clearBeforeDate(const Date& before) {
    if (SIZE_MAX == oldestPosition) return;
    while (oldestPosition != newestPosition) {
      if (data[oldestPosition].taken < before) {
        ++oldestPosition;
        if (data.size() == oldestPosition) {
          // overflowed
          oldestPosition = 0;
        }
      } else {
        return;
      }
    }
    // now we're on the last element
    if (data[oldestPosition].taken < before) {
      // remove last element
      oldestPosition = SIZE_MAX;
      newestPosition = SIZE_MAX;
    }
  }

  void clear() {
    oldestPosition = SIZE_MAX;
    newestPosition = SIZE_MAX;
  }

  SampleIterator<SampleList<SampleT,MaxSize>> begin() {
    return SampleIterator<SampleList<SampleT,MaxSize>>(*this);
  }

  SampleIterator<SampleList<SampleT,MaxSize>> end() {
    if (size() == 0) return SampleIterator<SampleList<SampleT,MaxSize>>(*this);
    return SampleIterator<SampleList<SampleT,MaxSize>>(*this,size()); // calls this object's size() function, not the array!
  }

private:
  std::array<SampleT,MaxSize> data;
  std::size_t oldestPosition;
  std::size_t newestPosition;
};

template <typename SampleListT,
          typename ValT> // from fwd decl =>  = typename SampleListT::value_type
struct SampleIterator {
  using difference_type = std::size_t;
  using value_type = ValT;
  using iterator_category = std::forward_iterator_tag;
  using pointer = value_type*;
  using reference = value_type&;

  SampleIterator(SampleListT& sl) : list(sl), pos(0) {}
  SampleIterator(SampleListT& sl, std::size_t from) : list(sl), pos(from) {} // used to get list.end() (size() + 1)
  SampleIterator(const SampleIterator<SampleListT>& other) : list(other.list), pos(other.pos) {} // copy ctor
  SampleIterator(SampleIterator<SampleListT>&& other) : list(other.list), pos(other.pos) {} // move ctor (cheaper to copy)
  ~SampleIterator() = default;

  // always returns const for safety
  const ValT& operator*() {
    return list[pos];
  }

  /// Implement operator+(int amt) to move this iterator forward
  SampleIterator<SampleListT>& operator+(int by) {
    pos += by;
    if (pos > list.size()) {
      pos = list.size(); // i.e. list.end()
    }
    return *this;
  }

  /// Implement operator+(int amt) to move this iterator forward
  SampleIterator<SampleListT>& operator-(int by) {
    if (by > pos) {
      pos = 0; // prevents underflow and a very large value of pos (as it's a std::size_t)
    } else {
      pos -= by;
    }
    return *this;
  }

  // to allow std::distance to work
  difference_type operator-(const SampleIterator<SampleListT>& other) {
    return pos - other.pos;
  }

  /// prefix operator
  SampleIterator<SampleListT>& operator++() {
    ++pos; // if it's one after the end of the list, then that's the same as list.end()
    return *this; // reference to instance
  }

  // postfix operator
  SampleIterator<SampleListT> operator++(int) {
    SampleIterator<SampleListT> cp =  *this; // copy of instance
    ++(*this);
    return cp;
  }

  bool operator==(const SampleIterator<SampleListT>& otherIter) const {
    return pos == otherIter.pos;
  }

  bool operator!=(const SampleIterator<SampleListT>& otherIter) const {
    return pos != otherIter.pos;
  }

private:
  SampleListT& list;
  std::size_t pos;
};

/// for std::distance
template<typename T>
typename SampleIterator<T>::difference_type distance(SampleIterator<T> first, SampleIterator<T> last) {
  return last - first;
}

} // end sampling namespace
}
}

#endif
