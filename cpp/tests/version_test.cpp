#include "quant_engine/version.hpp"

#include <iostream>
#include <string_view>

int main() {
    constexpr std::string_view expected{"0.1.0"};
    if (quant_engine::version() != expected) {
        std::cerr << "unexpected quant_engine version\n";
        return 1;
    }
    return 0;
}

