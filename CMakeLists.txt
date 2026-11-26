cmake_minimum_required(VERSION 3.19)
project(mkFileTools LANGUAGES CXX)
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

# Optimization level and CPU flags are up to whoever builds: the build type (Release: -O3) or, when building the
# package, CFLAGS/CXXFLAGS from makepkg.conf (build type None).
# MK_NATIVE: -march=native, uses SSE/AVX of the current CPU. The binaries may then not run on other CPUs, so it is
# off by default (build.sh turns it on for local builds).
option(MK_NATIVE "Optimize for the CPU of the build machine (-march=native)" OFF)
if(MK_NATIVE)
    add_compile_options(-march=native)
endif()

# NDEBUG for every build type except Debug, including None (package builds)
add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

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
            i18n/mkfiletools_de.ts
            i18n/mkfiletools_en.ts
    )
endif()
