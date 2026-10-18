add_executable(wallpascii_tests test_main.cpp)
target_link_libraries(wallpascii_tests PRIVATE wallpascii_core)

if(MSVC)
  target_compile_options(wallpascii_tests PRIVATE /W4 /utf-8)
else()
  target_compile_options(wallpascii_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME core_tests COMMAND wallpascii_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
