cmake_minimum_required(VERSION 3.16)
project(rt_portage_test)

set(CMAKE_CXX_STANDARD 17)

# Option de sélection du backend — défaut : linux
set(RT_BACKEND "linux" CACHE STRING "Backend RT à utiliser : linux ou xenomai")
set_property(CACHE RT_BACKEND PROPERTY STRINGS linux xenomai)

message(STATUS "RT_BACKEND sélectionné : ${RT_BACKEND}")

# Vérification que le backend demandé existe
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/backends/${RT_BACKEND}/rt_backend/rt_detect.hpp")
    message(FATAL_ERROR "Backend '${RT_BACKEND}' introuvable dans backends/${RT_BACKEND}/")
endif()

add_executable(rt_test main.cpp)

# Sélection du backend par include path — aucun #ifdef dans le code
# Le dossier du backend est placé AVANT le dossier public afin que son
# rt_backend/rt_detect.hpp masque la déclaration publique (même chemin relatif).
target_include_directories(rt_test PRIVATE backends/${RT_BACKEND})

# Interface publique (header commun, utilisée par les consommateurs externes)
target_include_directories(rt_test PUBLIC include)
