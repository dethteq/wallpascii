add_library(wallpascii_core STATIC
  src/adjust.cpp
  src/ascii.cpp
  src/color.cpp
  src/compose.cpp
  src/image.cpp
  src/layout.cpp
  src/render.cpp
  src/stb_impl.cpp
  src/text.cpp
  src/utf8.cpp
)

target_include_directories(wallpascii_core
  PUBLIC include
  PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb ${PROJECT_SOURCE_DIR}/third_party/font8x8
)

if(MSVC)
  target_compile_options(wallpascii_core PRIVATE /W4 /utf-8 /permissive-)
  target_compile_definitions(wallpascii_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
  target_compile_options(wallpascii_core PRIVATE -Wall -Wextra -Wpedantic)
  set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
