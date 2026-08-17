cmake_minimum_required(VERSION 3.19)
project(mkTools LANGUAGES CXX)

# --- 1. GLOBALE PERFORMANCE FLAGS & BUILD TYPE ---
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT result)
if(result)
    if(MINGW)
        # MinGW hat einen bekannten Bug mit LTO bei statischen Phasen ("plugin needed to handle lto object").
        # Da wir ohnehin -O3 nutzen, schalten wir IPO hier für Windows/MinGW ab.
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
    else()
        # Unter Linux (GCC/Clang) oder mit MSVC läuft es sauber durch
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

if(MSVC)
    add_compile_options(/O2 /Ot /GL /arch:AVX2)
else()
    # -O3: Aggressive Optimierung
    # -march=native: Nutzt SSE/AVX der aktuellen CPU
    # -flto: Link Time Optimization (removed here, since enabled by "set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)" above
    # -fomit-frame-pointer: Befreit ein Register für Daten (statt Stack-Tracking)
    add_compile_options(-O3 -march=native -fomit-frame-pointer)
endif()

# --- 2. QT & STANDARD SETUP ---
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent LinguistTools Network DBus)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

# --- PLATFORMSPEZIFISCHE PAKETE ---
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(KF6KIO REQUIRED)
endif()

# --- 3. DRITTANBIETER (ZLIB) GLOBAL BEREITSTELLEN ---
if(UNIX)
    find_package(ZLIB REQUIRED)
else()
    include(FetchContent)

    FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1 # Oder eine andere stabile Version
    )

    # Schaltet ZLIB-eigene Tests und Installation aus, damit es schneller geht
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(zlib)
endif()

# --- 4. SUBDIRECTORIES ---
# 1. Zuerst die gemeinsame Bibliothek einbinden
add_subdirectory(shared)

# 2. Danach die Apps, die darauf zugreifen
add_subdirectory(mkFileSearch)
add_subdirectory(mkFolderWidget)
add_subdirectory(mkLauncher)
add_subdirectory(mkTransactionHandler)

# =====================================================================
# ZENTRALE ÜBERSETZUNG FÜR DAS GESAMTE PROJEKT
# =====================================================================
if(TARGET shared)
    qt_add_translations(shared
        SOURCE_TARGETS
            shared
            mkFileSearch
            mkFolderWidget
            mkLauncher
            mkTransactionHandler
        TS_FILES
            i18n/mktools_de.ts
            i18n/mktools_en.ts
    )
endif()
