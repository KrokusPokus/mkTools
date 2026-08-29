# HIER KEIN cmake_minimum_required ODER project() ERNEUT AUFRUFEN!

# Statische Bibliothek definieren
add_library(shared STATIC
    src/customitemdelegates.h
    src/customproxystyle.h
    src/stylesheets.h)

# Sourcen zuweisen
target_sources(shared PRIVATE
    src/helpers.h src/helpers.cpp
    src/filepropertiesdialog.h src/filepropertiesdialog.cpp
    src/filesortproxymodel.h src/filesortproxymodel.cpp
    src/customtablemodel.h src/customtablemodel.cpp
    src/customtableview.h src/customtableview.cpp
    src/customlistview.h src/customlistview.cpp
    src/settingsmanager.h src/settingsmanager.cpp

)

# Wichtig, falls dein geteilter Code Q_OBJECT-Makros, UI oder Ressourcen nutzt
set_target_properties(shared PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# Qt-Abhängigkeiten für den gemeinsamen Code (z.B. Core)
target_link_libraries(shared
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        $<$<PLATFORM_ID:Linux>:KF6::KIOCore>
        $<$<PLATFORM_ID:Linux>:KF6::KIOWidgets>
        $<$<PLATFORM_ID:Linux>:KF6::KIOFileWidgets>
        $<$<PLATFORM_ID:Windows>:zlibstatic>
        $<$<PLATFORM_ID:Windows>:shlwapi>
        $<$<PLATFORM_ID:Linux>:ZLIB::ZLIB>
)

# Das ist der Clou: Jeder, der shared linkt, bekommt automatisch 
# Zugriff auf den "include"-Ordner, ohne Pfade raten zu müssen!
target_include_directories(shared PUBLIC src)

# Beschleunigt den Start unter Linux, indem ungenutzte Shared Libraries ignoriert werden
# INTERFACE sorgt dafür, dass die Apps das Flag beim Linken erben!
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(shared INTERFACE -Wl,--as-needed)
endif()
