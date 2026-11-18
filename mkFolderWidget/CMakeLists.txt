cmake_minimum_required(VERSION 3.19)
project(mkFolderWidget LANGUAGES CXX)

qt_add_executable(mkFolderWidget
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.h src/mainwindow.cpp
	
    res/resources.qrc
)

target_include_directories(mkFolderWidget PRIVATE src)

target_link_libraries(mkFolderWidget
    PRIVATE
		shared
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::DBus
        $<$<PLATFORM_ID:Windows>:zlibstatic>
        $<$<PLATFORM_ID:Linux>:ZLIB::ZLIB>
)

# Beschleunigt den Start unter Linux, indem ungenutzte Shared Libraries ignoriert werden
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(mkFolderWidget PRIVATE -Wl,--as-needed)
endif()

install(TARGETS mkFolderWidget
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # .desktop file: makes it selectable as default file manager; the file name must match the Wayland app_id (= executable name)
    install(FILES res/mkFolderWidget.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

#qt_generate_deploy_app_script(
#    TARGET mkFolderWidget
#    OUTPUT_SCRIPT deploy_script
#    NO_UNSUPPORTED_PLATFORM_ERROR
#)
#install(SCRIPT ${deploy_script})

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
