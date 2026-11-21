cmake_minimum_required(VERSION 3.19)
project(mkFileSearch LANGUAGES CXX)

qt_add_executable(mkFileSearch
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
        ZLIB::ZLIB
)

# Speeds up startup by ignoring unused shared libraries
target_link_options(mkFileSearch PRIVATE -Wl,--as-needed)

install(TARGETS mkFileSearch
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Start menu entry (icon: kfind from the icon theme); the file name must match the Wayland app_id (= executable name)
install(FILES res/mkFileSearch.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
