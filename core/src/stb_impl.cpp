// Single translation unit holding the stb implementations.
// File I/O goes through std::filesystem in image.cpp (UTF-8 paths on Windows),
// so stb only ever sees memory buffers.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
