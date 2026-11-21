cmake_minimum_required(VERSION 3.19)
project(mkTools LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. GLOBALE PERFORMANCE FLAGS & BUILD TYPE ---
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT result)
if(result)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# -O3: Aggressive optimization
# -march=native: Uses SSE/AVX of the current CPU
# -flto: Link Time Optimization (removed here, since enabled by "set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)" above
# -fomit-frame-pointer: Frees a register for data (instead of stack tracking)
add_compile_options(-O3 -march=native -fomit-frame-pointer)

# --- 2. QT & STANDARD SETUP ---
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent LinguistTools Network DBus)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

# --- KDE FRAMEWORKS ---
find_package(KF6KIO REQUIRED)

# --- 3. DRITTANBIETER (ZLIB) GLOBAL BEREITSTELLEN ---
find_package(ZLIB REQUIRED)

# --- 4. SUBDIRECTORIES ---
# 1. Add the shared library first
add_subdirectory(shared)

# 2. Then the apps that use it
add_subdirectory(mkBatchRename)
add_subdirectory(mkFileSearch)
add_subdirectory(mkFolderWidget)
add_subdirectory(mkLauncher)
add_subdirectory(mkTransactionHandler)

# KWin scripts (enable them under System Settings > Window Management > KWin Scripts).
# The folder name has to match the "Id" in metadata.json.
install(DIRECTORY kwin/mkFolderWidgetTiler/ DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts/mkfolderwidgettiler)
install(DIRECTORY kwin/mkNewWindowAutoPos/  DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts/mknewwindowautopos)

# =====================================================================
# CENTRAL TRANSLATION FOR THE WHOLE PROJECT
# =====================================================================
if(TARGET shared)
    qt_add_translations(shared
        SOURCE_TARGETS
            shared
            mkBatchRename
            mkFileSearch
            mkFolderWidget
            mkLauncher
            mkTransactionHandler
        TS_FILES
            i18n/mktools_de.ts
            i18n/mktools_en.ts
    )
endif()
