#pragma once
#include <iostream>
#include <string>

namespace rt {

inline std::string get_os_name() {
    std::cout << "[rt::backend] Xenomai 4 / EVL backend chargé." << std::endl;
    return "Xenomai 4 / EVL";
}

} // namespace rt
