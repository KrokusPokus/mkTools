cmake_minimum_required(VERSION 3.19)
project(mkFileSearch LANGUAGES CXX)

qt_add_executable(mkFileSearch
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.h src/mainwindow.cpp

    res/resources.qrc
)

target_include_directories(mkFileSearch PRIVATE src)

target_link_libraries(mkFileSearch
    PRIVATE
		shared
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        $<$<PLATFORM_ID:Windows>:zlibstatic>
        $<$<PLATFORM_ID:Linux>:ZLIB::ZLIB>
)

# Beschleunigt den Start unter Linux, indem ungenutzte Shared Libraries ignoriert werden
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(mkFileSearch PRIVATE -Wl,--as-needed)
endif()

install(TARGETS mkFileSearch
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Start menu entry (icon: kfind from the icon theme); the file name must match the Wayland app_id (= executable name)
    install(FILES res/mkFileSearch.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

#qt_generate_deploy_app_script(
#    TARGET mkFileSearch
#    OUTPUT_SCRIPT deploy_script
#    NO_UNSUPPORTED_PLATFORM_ERROR
#)
#install(SCRIPT ${deploy_script})

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
