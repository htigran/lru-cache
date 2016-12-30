/// The MIT License (MIT)
/// Copyright (c) 2016 Peter Goldsborough and Markus Engel
///
/// Permission is hereby granted, free of charge, to any person obtaining a copy
/// of this software and associated documentation files (the "Software"), to
/// deal in the Software without restriction, including without limitation the
/// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
/// sell copies of the Software, and to permit persons to whom the Software is
/// furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included in
/// all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
/// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
/// IN THE SOFTWARE.

#ifndef LRU_TIMED_CACHE_HPP
#define LRU_TIMED_CACHE_HPP

#include <cassert>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "lru/error.hpp"
#include "lru/internal/base-cache.hpp"
#include "lru/internal/last-accessed.hpp"

namespace LRU {
namespace Internal {
template <typename Key,
          typename Value,
          typename HashFunction,
          typename KeyEqual>
using TimedCacheBase =
    BaseCache<Key, Value, Internal::TimedInformation, HashFunction, KeyEqual>;
}

template <typename Key,
          typename Value,
          typename Duration = std::chrono::milliseconds,
          typename HashFunction = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class TimedCache
    : public Internal::TimedCacheBase<Key, Value, HashFunction, KeyEqual> {
 private:
  using super = Internal::TimedCacheBase<Key, Value, HashFunction, KeyEqual>;
  using PRIVATE_BASE_CACHE_MEMBERS;

 public:
  using PUBLIC_BASE_CACHE_MEMBERS;
  using typename super::size_t;

  template <typename AnyDurationType = Duration>
  explicit TimedCache(const AnyDurationType& time_to_live,
                      size_t capacity = Internal::DEFAULT_CAPACITY,
                      const HashFunction& hash = HashFunction(),
                      const KeyEqual& equal = KeyEqual())
  : super(capacity, hash, equal)
  , _time_to_live(std::chrono::duration_cast<Duration>(time_to_live)) {
  }

  template <typename Iterator, typename AnyDurationType = Duration>
  TimedCache(const AnyDurationType& time_to_live,
             size_t capacity,
             Iterator begin,
             Iterator end,
             const HashFunction& hash = HashFunction(),
             const KeyEqual& equal = KeyEqual())
  : super(capacity, begin, end, hash, equal)
  , _time_to_live(std::chrono::duration_cast<Duration>(time_to_live)) {
  }

  template <typename Iterator, typename AnyDurationType = Duration>
  TimedCache(const AnyDurationType& time_to_live,
             Iterator begin,
             Iterator end,
             const HashFunction& hash = HashFunction(),
             const KeyEqual& equal = KeyEqual())
  : super(begin, end, hash, equal)
  , _time_to_live(std::chrono::duration_cast<Duration>(time_to_live)) {
  }

  template <typename Range, typename AnyDurationType = Duration>
  explicit TimedCache(const AnyDurationType& time_to_live,
                      Range&& range,
                      const HashFunction& hash = HashFunction(),
                      const KeyEqual& equal = KeyEqual())
  : super(std::forward<Range>(range), hash, equal)
  , _time_to_live(std::chrono::duration_cast<Duration>(time_to_live)) {
  }

  template <typename Range, typename AnyDurationType = Duration>
  TimedCache(const AnyDurationType& time_to_live,
             size_t capacity,
             Range&& range,
             const HashFunction& hash = HashFunction(),
             const KeyEqual& equal = KeyEqual())
  : super(capacity, std::forward<Range>(range), hash, equal)
  , _time_to_live(std::chrono::duration_cast<Duration>(time_to_live)) {
  }

  template <typename AnyDurationType = Duration>
  TimedCache(const AnyDurationType& time_to_live,
             InitializerList list,
             const HashFunction& hash = HashFunction(),
             const KeyEqual& equal = KeyEqual())  // NOLINT(runtime/explicit)
      : super(list, hash, equal),
        _time_to_live(std::chrono::duration_cast<Duration>(time_to_live)) {
  }

  template <typename AnyDurationType = Duration>
  TimedCache(const AnyDurationType& time_to_live,
             size_t capacity,
             InitializerList list,
             const HashFunction& hash = HashFunction(),
             const KeyEqual& equal = KeyEqual())  // NOLINT(runtime/explicit)
      : super(capacity, list, hash, equal),
        _time_to_live(std::chrono::duration_cast<Duration>(time_to_live)) {
  }

  bool contains(const Key& key) const override {
    if (_last_accessed == key) return true;

    auto iterator = _cache.find(key);
    if (iterator != _cache.end() && !_has_expired(iterator->second)) {
      _last_accessed = iterator;
      return true;
    }

    // Note how we do not erase the element, even if it has expired.
    // The main reason why is that this would require `contains()` to be
    // non-const, or making two overloads (`const` version returns false for
    // expired elements, `non-const` returns *false* and erases the element).
    // However, there is no immediate benefit from erasing an expired element
    // here as opposed to doing it lazily in `insert()`. Also, if this element
    // is expired, so are also all elements before it. If we really wanted to
    // clean the cache, we can use `clean()` for this.

    return false;
  }

  Value& lookup(const Key& key) override {
    Information* information;

    if (key == _last_accessed) {
      information = &(_last_accessed.value());
    } else {
      auto iterator = _cache.find(key);
      if (iterator == _cache.end()) {
        throw LRU::Error::KeyNotFound();
      }
      _last_accessed = iterator;
      information = &(iterator->second);
    }

    if (_has_expired(*information)) {
      throw std::out_of_range{"Element has expired."};
    }

    return information->value;
  }

  const Value& lookup(const Key& key) const override {
    const Information* information;

    if (key == _last_accessed) {
      information = &(_last_accessed.value());
    } else {
      auto iterator = _cache.find(key);
      if (iterator == _cache.end()) {
        throw LRU::Error::KeyNotFound();
      }
      _last_accessed = iterator;
      information = &(iterator->second);
    }

    if (_has_expired(*information)) {
      throw std::out_of_range{"Element has expired."};
    }

    return information->value;
  }

  // no front() because we may have to erase the entire cache if everything
  // happens to be expired

  bool all_expired() const {
    if (is_empty()) return false;

    auto latest = _cache.lookup(_order.back());
    return has_expired(*latest);
  }

  /// Erases all expired elements from the cache.
  ///
  /// \complexity O(N)
  size_t clean() {
    // We have to do a linear search here because linked lists do not
    // support O(log N) binary searches given their node-based nature.
    // Either way, in the worst case the entire cache has expired and
    // we would have to do O(N) erasures.

    if (is_empty()) return;

    auto iterator = _order.begin();
    size_t number_of_erasures = 0;

    while (iterator != _order.end()) {
      // If the current element hasn't expired, also all elements inserted
      // after will not have, so we can stop.
      if (!_has_expired(_cache[*iterator])) break;

      // Erase and get the subsequent iterator
      iterator = _erase(iterator);
      number_of_erasures += 1;
    }

    return number_of_erasures;
  }

 private:
  using Clock = typename Internal::Clock;

  bool _has_expired(const Information& information) const noexcept {
    auto elapsed = Clock::now() - information.insertion_time;
    return std::chrono::duration_cast<Duration>(elapsed) > _time_to_live;
  }

  Duration _time_to_live;
};
}  // namespace LRU

#endif  // LRU_TIMED_CACHE_HPP
