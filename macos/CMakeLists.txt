set(APP_ICON ${PROJECT_SOURCE_DIR}/assets/icon/AppIcon.icns)
set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

add_executable(wallpascii MACOSX_BUNDLE
  src/main.mm
  src/AppDelegate.mm
  src/MainWindowController.mm
  src/CoreTextRasterizer.mm
  src/Renderer.mm
  ${APP_ICON}
)

set_target_properties(wallpascii PROPERTIES
  OUTPUT_NAME wallpascii
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  MACOSX_BUNDLE_BUNDLE_NAME wallpascii
  MACOSX_BUNDLE_GUI_IDENTIFIER com.dethteq.wallpascii
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  MACOSX_BUNDLE_ICON_FILE AppIcon
)

target_link_libraries(wallpascii PRIVATE
  wallpascii_core
  "-framework AppKit"
  "-framework CoreText"
  "-framework UniformTypeIdentifiers"
)
target_compile_options(wallpascii PRIVATE -fobjc-arc -Wall -Wextra)

# Ad-hoc signature so the bundle launches cleanly on Apple Silicon.
add_custom_command(TARGET wallpascii POST_BUILD
  COMMAND codesign --force --deep --sign - $<TARGET_BUNDLE_DIR:wallpascii>
  VERBATIM
)
