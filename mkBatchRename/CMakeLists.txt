cmake_minimum_required(VERSION 3.19)
project(mkBatchRename LANGUAGES CXX)

qt_add_executable(mkBatchRename
    src/main.cpp
    src/mainwindow.h src/mainwindow.cpp

    res/resources.qrc

)

target_include_directories(mkBatchRename PRIVATE src)

target_link_libraries(mkBatchRename
    PRIVATE
		shared
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        ZLIB::ZLIB
)

# Speeds up startup by ignoring unused shared libraries
target_link_options(mkBatchRename PRIVATE -Wl,--as-needed)

install(TARGETS mkBatchRename
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
