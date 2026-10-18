#pragma once

#include <string>
#include <vector>

namespace wallpascii {

// Invalid sequences decode to U+FFFD.
std::vector<char32_t> decodeUtf8(const std::string& text);
void appendUtf8(std::string& out, char32_t cp);

}  // namespace wallpascii
