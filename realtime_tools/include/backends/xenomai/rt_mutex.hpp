// Copyright 2026 — Portage realtime_tools vers Xenomai/EVL
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#ifndef RT_BACKENDS_XENOMAI__RT_MUTEX_HPP_
#define RT_BACKENDS_XENOMAI__RT_MUTEX_HPP_

#include <pthread.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>

#include <evl/mutex.h>

namespace rt
{
namespace detail
{

// Tags de type — permettent aux static_assert des tests de fonctionner
// sur les deux backends (posix et xenomai).
struct error_mutex_type_t {};
struct recursive_mutex_type_t {};

}  // namespace detail

// ---------------------------------------------------------------------------
// rt::mutex — evl_mutex avec priority inheritance (PI par défaut dans EVL)
// ---------------------------------------------------------------------------
// Utilisable depuis thread RT et non-RT.
// lock() / try_lock() requièrent que le thread appelant soit attaché à EVL
// (rt::thread_attach() doit avoir été appelé au démarrage du thread).
// ---------------------------------------------------------------------------
class mutex
{
public:
  using native_handle_type = struct evl_mutex *;
  using type = detail::error_mutex_type_t;

  mutex()
  {
    // Le nom doit être unique dans /dev/evl/ — on utilise l'adresse de l'objet.
    char name[64];
    std::snprintf(name, sizeof(name), "rtmutex_%p", static_cast<void *>(this));
    const int ret = evl_new_mutex(&mutex_, name);
    if (ret < 0) {
      throw std::system_error(-ret, std::system_category(), "evl_new_mutex failed");
    }
  }

  ~mutex()
  {
    evl_destroy_mutex(&mutex_);
  }

  mutex(const mutex &) = delete;
  mutex & operator=(const mutex &) = delete;

  native_handle_type native_handle() noexcept { return &mutex_; }

  void lock()
  {
    const int ret = evl_lock_mutex(&mutex_);
    if (ret != 0) {
      throw std::system_error(-ret, std::system_category(), "evl_lock_mutex failed");
    }
  }

  void unlock() noexcept
  {
    const int ret = evl_unlock_mutex(&mutex_);
    if (ret != 0) {
      std::fprintf(
        stderr, "[rt::mutex] evl_unlock_mutex failed: %s (errno=%d)\n",
        std::strerror(-ret), -ret);
    }
  }

  // Strictement non-bloquant — appelé depuis le chemin RT de RealtimeBuffer.
  bool try_lock()
  {
    const int ret = evl_trylock_mutex(&mutex_);
    if (ret == 0) {
      return true;
    }
    if (ret == -EBUSY) {
      return false;
    }
    throw std::system_error(-ret, std::system_category(), "evl_trylock_mutex failed");
  }

private:
  struct evl_mutex mutex_;
};

// ---------------------------------------------------------------------------
// rt::recursive_mutex — evl_mutex avec comptage de récursion manuel.
// EVL ne fournit pas de mutex récursif natif : on encapsule un evl_mutex
// avec suivi du thread propriétaire (pthread_self()) et un compteur.
// ---------------------------------------------------------------------------
class recursive_mutex
{
public:
  using native_handle_type = struct evl_mutex *;
  using type = detail::recursive_mutex_type_t;

  recursive_mutex()
  {
    char name[64];
    std::snprintf(name, sizeof(name), "rtrec_%p", static_cast<void *>(this));
    const int ret = evl_new_mutex(&mutex_, name);
    if (ret < 0) {
      throw std::system_error(-ret, std::system_category(), "evl_new_mutex (recursive) failed");
    }
  }

  ~recursive_mutex()
  {
    evl_destroy_mutex(&mutex_);
  }

  recursive_mutex(const recursive_mutex &) = delete;
  recursive_mutex & operator=(const recursive_mutex &) = delete;

  native_handle_type native_handle() noexcept { return &mutex_; }

  void lock()
  {
    const pthread_t self = pthread_self();
    if (owner_.load(std::memory_order_relaxed) == self) {
      ++count_;
      return;
    }
    const int ret = evl_lock_mutex(&mutex_);
    if (ret != 0) {
      throw std::system_error(-ret, std::system_category(), "evl_lock_mutex (recursive) failed");
    }
    owner_.store(self, std::memory_order_relaxed);
    count_ = 1;
  }

  void unlock() noexcept
  {
    if (--count_ == 0) {
      owner_.store(pthread_t{}, std::memory_order_relaxed);
      const int ret = evl_unlock_mutex(&mutex_);
      if (ret != 0) {
        std::fprintf(
          stderr, "[rt::recursive_mutex] evl_unlock_mutex failed: %s (errno=%d)\n",
          std::strerror(-ret), -ret);
      }
    }
  }

  bool try_lock()
  {
    const pthread_t self = pthread_self();
    if (owner_.load(std::memory_order_relaxed) == self) {
      ++count_;
      return true;
    }
    const int ret = evl_trylock_mutex(&mutex_);
    if (ret == 0) {
      owner_.store(self, std::memory_order_relaxed);
      count_ = 1;
      return true;
    }
    if (ret == -EBUSY) {
      return false;
    }
    throw std::system_error(-ret, std::system_category(), "evl_trylock_mutex (recursive) failed");
  }

private:
  struct evl_mutex mutex_;
  std::atomic<pthread_t> owner_{};
  int count_{0};
};

}  // namespace rt

#endif  // RT_BACKENDS_XENOMAI__RT_MUTEX_HPP_
