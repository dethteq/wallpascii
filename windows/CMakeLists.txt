set(ICON_PATH ${PROJECT_SOURCE_DIR}/assets/icon/AppIcon.ico)
set(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/res/app.manifest)
configure_file(res/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)

add_executable(wallpascii WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/Ui.cpp
  src/DWriteRasterizer.cpp
  src/Renderer.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/app.rc PROPERTIES
  OBJECT_DEPENDS "${ICON_PATH};${MANIFEST_PATH}")

target_link_libraries(wallpascii PRIVATE
  wallpascii_core d2d1 dwrite windowscodecs ole32 oleaut32 comdlg32 shell32 dwmapi uxtheme comctl32 user32 gdi32 advapi32
)
target_compile_definitions(wallpascii PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)

if(MSVC)
  target_compile_options(wallpascii PRIVATE /W4 /utf-8 /permissive-)
  # The manifest comes from app.rc.
  target_link_options(wallpascii PRIVATE /MANIFEST:NO)
else()
  target_compile_options(wallpascii PRIVATE -Wall -Wextra)
  target_link_options(wallpascii PRIVATE -municode -static)
endif()
