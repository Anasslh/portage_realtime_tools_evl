// Copyright 2026 — Portage realtime_tools vers Xenomai/EVL
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#ifndef RT_BACKENDS_POSIX__RT_THREAD_HPP_
#define RT_BACKENDS_POSIX__RT_THREAD_HPP_

#include <string>

namespace rt
{

/**
 * Attache le thread appelant au domaine temps réel.
 * Backend posix : no-op — retourne true immédiatement.
 * L'appel est idempotent et sans effet de bord.
 */
inline bool thread_attach(const std::string & /*name*/ = "")
{
  return true;
}

}  // namespace rt

#endif  // RT_BACKENDS_POSIX__RT_THREAD_HPP_
