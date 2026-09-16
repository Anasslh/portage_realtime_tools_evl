#pragma once
#include <string>

namespace rt {

/**
 * Retourne le nom de l'OS/backend détecté à la compilation.
 * Affiche aussi un message sur std::cout.
 * Implémentation sélectionnée par CMake via RT_BACKEND.
 */
std::string get_os_name();

} // namespace rt
