add_executable(wallpascii-cli main.cpp)
target_link_libraries(wallpascii-cli PRIVATE wallpascii_core)

if(MSVC)
  target_compile_options(wallpascii-cli PRIVATE /W4 /utf-8)
else()
  target_compile_options(wallpascii-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
