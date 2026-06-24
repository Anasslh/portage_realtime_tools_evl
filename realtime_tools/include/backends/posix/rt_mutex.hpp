// Copyright 2026 — Portage realtime_tools vers Xenomai/EVL
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#ifndef RT_BACKENDS_POSIX__RT_MUTEX_HPP_
#define RT_BACKENDS_POSIX__RT_MUTEX_HPP_

#ifdef _WIN32
#error "rt_mutex.hpp (posix backend) is not supported on Windows"
#endif

#include <pthread.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace rt
{
namespace detail
{

struct error_mutex_type_t
{
  static constexpr int value = PTHREAD_MUTEX_ERRORCHECK;
};

struct recursive_mutex_type_t
{
  static constexpr int value = PTHREAD_MUTEX_RECURSIVE;
};

struct robust_robustness_t
{
#if defined(__linux__)
  static constexpr int value = PTHREAD_MUTEX_ROBUST;
#else
  static constexpr int value = 0;
#endif
};

/**
 * pthread mutex avec priority inheritance, error-check et robustness.
 * Concept C++ BasicLockable — compatible std::unique_lock / std::lock_guard.
 */
template <typename MutexType, typename MutexRobustness>
class mutex
{
public:
  using native_handle_type = pthread_mutex_t *;
  using type = MutexType;
  using robustness = MutexRobustness;

  mutex()
  {
    pthread_mutexattr_t attr;

    const auto attr_destroy = [](pthread_mutexattr_t * a) {
      const auto res = pthread_mutexattr_destroy(a);
      if (res != 0) {
        throw std::system_error(res, std::generic_category(), "Failed to destroy mutex attribute");
      }
    };
    using attr_cleanup_t = std::unique_ptr<pthread_mutexattr_t, decltype(attr_destroy)>;
    auto attr_cleanup = attr_cleanup_t(&attr, attr_destroy);

    auto res = pthread_mutexattr_init(&attr);
    if (res != 0) {
      throw std::system_error(res, std::system_category(), "Failed to initialize mutex attribute");
    }

    res = pthread_mutexattr_settype(&attr, MutexType::value);
    if (res != 0) {
      throw std::system_error(res, std::system_category(), "Failed to set mutex type");
    }

    res = pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    if (res != 0) {
      throw std::system_error(res, std::system_category(), "Failed to set mutex protocol");
    }

#if defined(__linux__)
    res = pthread_mutexattr_setrobust(&attr, MutexRobustness::value);
    if (res != 0) {
      throw std::system_error(res, std::system_category(), "Failed to set mutex robustness");
    }
#endif

    res = pthread_mutex_init(&mutex_, &attr);
    if (res != 0) {
      throw std::system_error(res, std::system_category(), "Failed to initialize mutex");
    }
  }

  ~mutex()
  {
    const auto res = pthread_mutex_destroy(&mutex_);
    if (res != 0) {
      std::cerr << "Failed to destroy mutex : " << std::strerror(res) << std::endl;
    }
  }

  mutex(const mutex &) = delete;
  mutex & operator=(const mutex &) = delete;

  native_handle_type native_handle() noexcept { return &mutex_; }

  void lock()
  {
    const auto res = pthread_mutex_lock(&mutex_);
    if (res == 0) {
      return;
    }
    if (res == EOWNERDEAD) {
#if defined(__linux__)
      const auto res_c = pthread_mutex_consistent(&mutex_);
      if (res_c != 0) {
        throw std::runtime_error(
          std::string("Failed to make mutex consistent : ") + std::strerror(res_c));
      }
      std::cerr << "Mutex owner died, mutex is consistent now. This shouldn't happen!" << std::endl;
#else
      std::cerr << "Mutex owner died, pthread_mutex_consistent not supported." << std::endl;
#endif
    } else if (res == EDEADLK) {
      throw std::system_error(res, std::system_category(), "Deadlock detected");
    } else {
      throw std::runtime_error(std::string("Failed to lock mutex : ") + std::strerror(res));
    }
  }

  void unlock() noexcept
  {
    const auto res = pthread_mutex_unlock(&mutex_);
    if (res != 0) {
      std::cerr << "Failed to unlock mutex : " << std::strerror(res) << std::endl;
    }
  }

  bool try_lock()
  {
    const auto res = pthread_mutex_trylock(&mutex_);
    if (res == 0) {
      return true;
    }
    if (res == EBUSY) {
      return false;
    }
    if (res == EOWNERDEAD) {
#if defined(__linux__)
      const auto res_c = pthread_mutex_consistent(&mutex_);
      if (res_c != 0) {
        throw std::runtime_error(
          std::string("Failed to make mutex consistent : ") + std::strerror(res_c));
      }
      std::cerr << "Mutex owner died, mutex is consistent now. This shouldn't happen!" << std::endl;
#else
      std::cerr << "Mutex owner died, pthread_mutex_consistent not supported." << std::endl;
#endif
      return true;
    }
    if (res == EDEADLK) {
      throw std::system_error(res, std::system_category(), "Deadlock detected");
    }
    throw std::runtime_error(std::string("Failed to try_lock mutex : ") + std::strerror(res));
  }

private:
  pthread_mutex_t mutex_;
};

}  // namespace detail

// Types publics du backend posix — utilisés via les alias dans mutex.hpp
using mutex = detail::mutex<detail::error_mutex_type_t, detail::robust_robustness_t>;
using recursive_mutex = detail::mutex<detail::recursive_mutex_type_t, detail::robust_robustness_t>;

}  // namespace rt

#endif  // RT_BACKENDS_POSIX__RT_MUTEX_HPP_
