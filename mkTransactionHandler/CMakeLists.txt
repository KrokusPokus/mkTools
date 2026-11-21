cmake_minimum_required(VERSION 3.19)
project(mkTransactionHandler LANGUAGES CXX)

qt_add_executable(mkTransactionHandler
    src/main.cpp
    src/mainwindow.h src/mainwindow.cpp
    src/fileoperation.h src/fileoperation.cpp
    src/conflictdialog.h src/conflictdialog.cpp
	
    res/resources.qrc
)

target_include_directories(mkTransactionHandler PRIVATE src)

target_link_libraries(mkTransactionHandler
    PRIVATE
		shared
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        ZLIB::ZLIB
)

# Beschleunigt den Start, indem ungenutzte Shared Libraries ignoriert werden
target_link_options(mkTransactionHandler PRIVATE -Wl,--as-needed)

install(TARGETS mkTransactionHandler
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# .desktop file: gives the window its identity (icon, progress display); the file name must match the Wayland app_id (= executable name)
install(FILES res/mkTransactionHandler.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

#qt_generate_deploy_app_script(
#    TARGET mkTransactionHandler
#    OUTPUT_SCRIPT deploy_script
#    NO_UNSUPPORTED_PLATFORM_ERROR
#)
#install(SCRIPT ${deploy_script})

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
