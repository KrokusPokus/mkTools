cmake_minimum_required(VERSION 3.19)
project(mkFolderWidget LANGUAGES CXX)

qt_add_executable(mkFolderWidget
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
        ZLIB::ZLIB
)

# Beschleunigt den Start, indem ungenutzte Shared Libraries ignoriert werden
target_link_options(mkFolderWidget PRIVATE -Wl,--as-needed)

install(TARGETS mkFolderWidget
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# .desktop file: makes it selectable as default file manager; the file name must match the Wayland app_id (= executable name)
install(FILES res/mkFolderWidget.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
