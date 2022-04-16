#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Toastbox {

std::vector<std::string> StringSplit(std::string_view str, std::string_view delim) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    for (;;) {
        size_t posNew = str.find(delim, pos);
        if (posNew == std::string::npos) {
            tokens.emplace_back(str.substr(pos));
            break;
        }
        tokens.push_back(std::string(str.substr(pos, posNew-pos)));
        pos = posNew+delim.size();
    }
    return tokens;
}

} // namespace Toastbox
