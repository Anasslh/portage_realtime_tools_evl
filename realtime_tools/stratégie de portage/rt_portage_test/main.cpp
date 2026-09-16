#include <iostream>
#include "rt_backend/rt_detect.hpp"

int main() {
    std::cout << "=== Test de la stratégie de portage ===" << std::endl;

    std::string os = rt::get_os_name();

    std::cout << "Backend retourné : " << os << std::endl;
    std::cout << "=======================================" << std::endl;

    return 0;
}
