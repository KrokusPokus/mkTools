# DO NOT CALL cmake_minimum_required OR project() AGAIN HERE!

# Define the static library
add_library(shared STATIC
    src/customitemdelegates.h
    src/customproxystyle.h
    src/stylesheets.h)

# Assign the sources
target_sources(shared PRIVATE
    src/helpers.h src/helpers.cpp
    src/filepropertiesdialog.h src/filepropertiesdialog.cpp
    src/filesortproxymodel.h src/filesortproxymodel.cpp
    src/customtablemodel.h src/customtablemodel.cpp
    src/customtableview.h src/customtableview.cpp
    src/customlistview.h src/customlistview.cpp
    src/settingsmanager.h src/settingsmanager.cpp

)

# Important in case your shared code uses Q_OBJECT macros, UI or resources
set_target_properties(shared PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# Qt dependencies for the shared code (e.g. Core)
target_link_libraries(shared
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        KF6::KIOCore
        KF6::KIOWidgets
        KF6::KIOFileWidgets
        ZLIB::ZLIB
)

# This is the trick: everyone who links shared automatically gets 
# access to the "include" folder without having to guess paths!
target_include_directories(shared PUBLIC src)

# Speeds up startup by ignoring unused shared libraries
# INTERFACE makes sure that the apps inherit the flag when linking!
target_link_options(shared INTERFACE -Wl,--as-needed)
