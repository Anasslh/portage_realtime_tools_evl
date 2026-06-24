// Copyright 2024 PAL Robotics S.L.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/// \author Sai Kishor Kothakota

#ifndef REALTIME_TOOLS__MUTEX_HPP_
#define REALTIME_TOOLS__MUTEX_HPP_

#ifdef _WIN32
#error "The mutex.hpp header is not supported on Windows platforms"
#endif

// Le backend est sélectionné par CMake via target_include_directories.
// Sur posix   : inclut backends/posix/rt_mutex.hpp   (pthread + PRIO_INHERIT)
// Sur xenomai : inclut backends/xenomai/rt_mutex.hpp  (evl_mutex)
#include <rt_mutex.hpp>

namespace realtime_tools
{
// API publique inchangée — les utilisateurs de ros2_control ne modifient rien.
using prio_inherit_mutex = rt::mutex;
using prio_inherit_recursive_mutex = rt::recursive_mutex;

// Aliases de compatibilité pour les tests qui inspectent les types internes.
namespace detail
{
using error_mutex_type_t = rt::detail::error_mutex_type_t;
using recursive_mutex_type_t = rt::detail::recursive_mutex_type_t;
}  // namespace detail
}  // namespace realtime_tools

#endif  // REALTIME_TOOLS__MUTEX_HPP_
