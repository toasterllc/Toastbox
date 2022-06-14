#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <sstream>

namespace Toastbox::String {

inline bool StartsWith(std::string_view prefix, std::string_view str) {
    if (str.size() < prefix.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), str.begin());
}

inline bool EndsWith(std::string_view suffix, std::string_view str) {
    if (str.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

inline std::vector<std::string> Split(std::string_view str, std::string_view delim) {
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

inline std::string Join(const std::vector<std::string>& strs, std::string_view delim) {
    std::stringstream ss;
    bool first = true;
    for (const std::string& str : strs) {
        if (!first) ss << delim;
        ss << str;
        first = false;
    }
    return ss.str();
}

inline std::string Trim(std::string_view str, const char* set="\t\n ") {
    std::string x(str);
    x.erase(x.find_last_not_of(set)+1);
    x.erase(0, x.find_first_not_of(set));
    return x;
}

} // namespace Toastbox
