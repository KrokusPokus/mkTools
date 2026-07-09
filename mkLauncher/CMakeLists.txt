cmake_minimum_required(VERSION 3.19)
project(mkLauncher LANGUAGES CXX)

qt_add_executable(mkLauncher
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.h src/mainwindow.cpp
	
    res/resources.qrc
)

target_include_directories(mkLauncher PRIVATE src)

target_link_libraries(mkLauncher
    PRIVATE
		shared
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::DBus
        Qt6::Network
        $<$<PLATFORM_ID:Windows>:zlibstatic>
        $<$<PLATFORM_ID:Linux>:ZLIB::ZLIB>
)

# Beschleunigt den Start unter Linux, indem ungenutzte Shared Libraries ignoriert werden
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(mkLauncher PRIVATE -Wl,--as-needed)
endif()

install(TARGETS mkLauncher
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

#qt_generate_deploy_app_script(
#    TARGET mkLauncher
#    OUTPUT_SCRIPT deploy_script
#    NO_UNSUPPORTED_PLATFORM_ERROR
#)
#install(SCRIPT ${deploy_script})

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

if(LINUX)
    add_custom_command(TARGET mkLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:mkLauncher>
        $ENV{HOME}/.local/bin/mkLauncher
        COMMENT "Copying mkLauncher to ~/.local/bin/"
    )
endif()
