cmake_minimum_required(VERSION 3.16)
project(realtime_tools LANGUAGES CXX)

find_package(ros2_control_cmake REQUIRED)
set_compiler_options()

if(WIN32)
  add_compile_definitions(
    # For math constants
    _USE_MATH_DEFINES
    # Minimize Windows namespace collision
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
  # set the same behavior for windows as it is on linux
  export_windows_symbols()
endif()

set(THIS_PACKAGE_INCLUDE_DEPENDS
  rclcpp
  rclcpp_action
  Threads
  rcpputils
)

find_package(ament_cmake REQUIRED)
foreach(Dependency IN ITEMS ${THIS_PACKAGE_INCLUDE_DEPENDS})
  find_package(${Dependency} REQUIRED)
endforeach()

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost COMPONENTS headers)
if(NOT Boost_headers_FOUND)
  find_package(Boost REQUIRED)
endif()

add_library(realtime_tools SHARED
  src/realtime_helpers.cpp
)
target_include_directories(realtime_tools PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/realtime_tools>
)
target_link_libraries(realtime_tools PUBLIC rclcpp::rclcpp rclcpp_action::rclcpp_action rcpputils::rcpputils Threads::Threads Boost::boost)
if(UNIX AND NOT APPLE)
  target_link_libraries(realtime_tools PUBLIC cap)
endif()

# A library to detect a realtime kernel and set thread priority, if one is found
add_library(thread_priority SHARED
  src/realtime_helpers.cpp
)
target_include_directories(thread_priority PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/realtime_tools>
)
target_link_libraries(thread_priority PUBLIC rclcpp::rclcpp rclcpp_action::rclcpp_action rcpputils::rcpputils Threads::Threads)
if(UNIX AND NOT APPLE)
  target_link_libraries(thread_priority PUBLIC cap)
endif()

# ── Détection optionnelle de Xenomai EVL ─────────────────────────────────────
# libevl est la bibliothèque userspace du co-kernel Xenomai 4 (EVL).
# Elle est installée dans /usr/lib ou /usr/local/lib sur un système Xenomai.
# Si absente, tous les tests EVL-spécifiques se compilent mais se sautent
# automatiquement à l'exécution via SKIP_IF_NOT_EVL() / EvlTestFixture.
find_library(EVL_LIBRARY NAMES evl PATHS /usr/lib /usr/local/lib)
find_path(EVL_INCLUDE_DIR NAMES evl/evl.h PATHS /usr/include /usr/local/include)

if(EVL_LIBRARY AND EVL_INCLUDE_DIR)
  set(HAVE_EVL TRUE)
  message(STATUS "Xenomai EVL trouvé : lib=${EVL_LIBRARY}, include=${EVL_INCLUDE_DIR}")

  # Vérifier si evl_get_stats() est disponible (API moderne, libevl ≥ r33).
  # On fait une vérification à la compilation avec try_compile pour éviter
  # les surprises sur des versions plus anciennes d'EVL.
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LIBRARIES ${EVL_LIBRARY})
  set(CMAKE_REQUIRED_INCLUDES ${EVL_INCLUDE_DIR})
  check_cxx_source_compiles("
    #include <evl/thread.h>
    int main() {
      struct evl_thread_stats s{};
      evl_get_stats(0, &s);
      return s.nssw;
    }
  " HAVE_EVL_GET_STATS)
  unset(CMAKE_REQUIRED_LIBRARIES)
  unset(CMAKE_REQUIRED_INCLUDES)

  if(HAVE_EVL_GET_STATS)
    message(STATUS "  evl_get_stats() disponible (API moderne)")
  else()
    message(STATUS "  evl_get_stats() absent — utilisation du fallback procfs")
  endif()
else()
  set(HAVE_EVL FALSE)
  message(STATUS "Xenomai EVL non trouvé — les tests EVL seront skippés à l'exécution")
endif()

# Helper de fonction : applique les définitions/includes/libs EVL à une cible.
# Usage : target_configure_evl(ma_cible)
function(target_configure_evl target_name)
  if(HAVE_EVL)
    target_compile_definitions(${target_name} PRIVATE HAVE_EVL)
    target_include_directories(${target_name} PRIVATE ${EVL_INCLUDE_DIR})
    target_link_libraries(${target_name} ${EVL_LIBRARY})
    if(HAVE_EVL_GET_STATS)
      target_compile_definitions(${target_name} PRIVATE HAVE_EVL_GET_STATS)
    endif()
  endif()
endfunction()

# Unit Tests
if(BUILD_TESTING)
  find_package(ament_cmake_gmock REQUIRED)
  find_package(lifecycle_msgs REQUIRED)
  find_package(rclcpp_lifecycle REQUIRED)
  find_package(test_msgs REQUIRED)

  ament_add_gmock(thread_priority_tests test/thread_priority_tests.cpp)
  target_link_libraries(thread_priority_tests thread_priority)

  ament_add_gmock(realtime_thread_safe_box_tests test/realtime_thread_safe_box_tests.cpp)
  target_link_libraries(realtime_thread_safe_box_tests realtime_tools)

  ament_add_gmock(realtime_buffer_tests test/realtime_buffer_tests.cpp)
  target_link_libraries(realtime_buffer_tests realtime_tools)

  ament_add_gmock(lock_free_queue_tests test/lock_free_queue_tests.cpp)

  target_link_libraries(lock_free_queue_tests realtime_tools Boost::boost)
  if(UNIX AND NOT APPLE)
    target_link_libraries(lock_free_queue_tests atomic)
  endif()

  ament_add_gmock(realtime_publisher_tests
                  test/realtime_publisher.test
                  test/realtime_publisher_tests.cpp)
  target_link_libraries(realtime_publisher_tests realtime_tools ${test_msgs_TARGETS})

  ament_add_gmock(realtime_server_goal_handle_tests
                  test/realtime_server_goal_handle.test
                  test/realtime_server_goal_handle_tests.cpp)
  target_link_libraries(realtime_server_goal_handle_tests realtime_tools ${test_msgs_TARGETS})

  ament_add_gmock(test_async_function_handler test/test_async_function_handler.cpp)
  target_link_libraries(test_async_function_handler
                        realtime_tools
                        thread_priority
                        ${lifecycle_msgs_TARGETS}
                        rclcpp_lifecycle::rclcpp_lifecycle)

  if(NOT WIN32)
    ament_add_gmock(realtime_mutex_tests test/realtime_mutex_tests.cpp)
    target_link_libraries(realtime_mutex_tests realtime_tools)
  endif()

  # ── Tests EVL Phase 1 : validation de l'infrastructure RT ──────────────────
  # Ce test valide evl_test_helpers.hpp lui-même avant de l'utiliser ailleurs.
  # Il peut être compilé sur tout OS mais skip proprement sans kernel EVL.
  if(NOT WIN32)
    ament_add_gmock(evl_environment_tests test/evl_environment_tests.cpp)
    target_link_libraries(evl_environment_tests realtime_tools)
    # Applique HAVE_EVL, HAVE_EVL_GET_STATS, includes et libs EVL si disponibles.
    target_configure_evl(evl_environment_tests)
  endif()

endif()

# Install
install(
  DIRECTORY include/
  DESTINATION include/realtime_tools
)
install(TARGETS realtime_tools thread_priority
  EXPORT export_realtime_tools
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

ament_export_targets(export_realtime_tools HAS_LIBRARY_TARGET)
ament_export_dependencies(${THIS_PACKAGE_INCLUDE_DEPENDS} Boost)
ament_package()
