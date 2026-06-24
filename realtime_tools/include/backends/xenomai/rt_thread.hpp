// Copyright 2026 — Portage realtime_tools vers Xenomai/EVL
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#ifndef RT_BACKENDS_XENOMAI__RT_THREAD_HPP_
#define RT_BACKENDS_XENOMAI__RT_THREAD_HPP_

#include <string>
#include <evl/thread.h>

namespace rt
{

/**
 * Attache le thread appelant au domaine temps réel EVL.
 * Doit être appelé en toute première ligne de la fonction du thread,
 * avant tout accès à une primitive EVL (mutex, flag, etc.).
 *
 * Idempotent : si le thread est déjà attaché (evl_get_self() >= 0),
 * retourne true sans rappeler evl_attach_self().
 *
 * Retourne false si l'attachement échoue (log dans stderr).
 */
inline bool thread_attach(const std::string & name = "")
{
  // Idempotence : déjà attaché → rien à faire
  if (evl_get_self() >= 0) {
    return true;
  }

  const char * thread_name = name.empty() ? "rt_thread" : name.c_str();
  const int fd = evl_attach_self(thread_name);

  if (fd < 0) {
    // errno est positionné par evl_attach_self — EPERM si kernel non-EVL,
    // EBUSY si le nom est déjà pris, EINVAL si le nom est invalide.
    std::fprintf(
      stderr,
      "[rt::thread_attach] evl_attach_self(\"%s\") failed: %s (errno=%d)\n",
      thread_name, std::strerror(-fd), -fd);
    return false;
  }

  return true;
}

}  // namespace rt

#endif  // RT_BACKENDS_XENOMAI__RT_THREAD_HPP_
