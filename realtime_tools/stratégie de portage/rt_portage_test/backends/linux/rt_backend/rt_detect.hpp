#pragma once
#include <iostream>
#include <string>

namespace rt {

inline std::string get_os_name() {
    std::cout << "[rt::backend] Linux / PREEMPT_RT backend chargé." << std::endl;
    return "Linux PREEMPT_RT";
}

} // namespace rt
