cmake_minimum_required(VERSION 3.25)

### Project

set(TREESHEETS_VERSION "" CACHE STRING "Version string (e.g., 1.2.3). Leave empty for auto-timestamp.")

if("${TREESHEETS_VERSION}" STREQUAL "")
    string(TIMESTAMP TREESHEETS_VERSION "%y%m%d.%H%M" UTC)
    message(STATUS "Auto-generated version: ${TREESHEETS_VERSION}")
else()
    message(STATUS "User-defined version: ${TREESHEETS_VERSION}")
endif()

project(TreeSheets
    DESCRIPTION "A free-form hierarchical data organizer"
    HOMEPAGE_URL "https://github.com/aardappel/treesheets"
    VERSION "${TREESHEETS_VERSION}")

if(WIN32 AND MSVC)
    string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" TREESHEETS_MSVC_ARCH)
    if(TREESHEETS_MSVC_ARCH STREQUAL "amd64")
        set(TREESHEETS_MSVC_ARCH "x64")
    endif()

    if(CMAKE_CXX_COMPILER)
        cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH TREESHEETS_VC_BIN_ARCH_DIR)
        cmake_path(GET TREESHEETS_VC_BIN_ARCH_DIR PARENT_PATH TREESHEETS_VC_BIN_HOST_DIR)
        cmake_path(GET TREESHEETS_VC_BIN_HOST_DIR PARENT_PATH TREESHEETS_VC_BIN_DIR)
        cmake_path(GET TREESHEETS_VC_BIN_DIR PARENT_PATH TREESHEETS_VC_TOOLS_DIR)

        if(EXISTS "${TREESHEETS_VC_TOOLS_DIR}/include")
            include_directories(SYSTEM "${TREESHEETS_VC_TOOLS_DIR}/include")
        endif()
        if(EXISTS "${TREESHEETS_VC_TOOLS_DIR}/lib/${TREESHEETS_MSVC_ARCH}")
            link_directories("${TREESHEETS_VC_TOOLS_DIR}/lib/${TREESHEETS_MSVC_ARCH}")
        endif()
    endif()

    if(CMAKE_RC_COMPILER)
        cmake_path(GET CMAKE_RC_COMPILER PARENT_PATH TREESHEETS_WINSDK_BIN_ARCH_DIR)
        cmake_path(GET TREESHEETS_WINSDK_BIN_ARCH_DIR PARENT_PATH TREESHEETS_WINSDK_BIN_VERSION_DIR)
        cmake_path(GET TREESHEETS_WINSDK_BIN_VERSION_DIR FILENAME TREESHEETS_WINSDK_VERSION)
        cmake_path(GET TREESHEETS_WINSDK_BIN_VERSION_DIR PARENT_PATH TREESHEETS_WINSDK_BIN_DIR)
        cmake_path(GET TREESHEETS_WINSDK_BIN_DIR PARENT_PATH TREESHEETS_WINSDK_ROOT)

        foreach(TREESHEETS_WINSDK_INCLUDE_KIND ucrt shared um winrt cppwinrt)
            set(TREESHEETS_WINSDK_INCLUDE_DIR
                "${TREESHEETS_WINSDK_ROOT}/Include/${TREESHEETS_WINSDK_VERSION}/${TREESHEETS_WINSDK_INCLUDE_KIND}")
            if(EXISTS "${TREESHEETS_WINSDK_INCLUDE_DIR}")
                include_directories(SYSTEM "${TREESHEETS_WINSDK_INCLUDE_DIR}")
            endif()
        endforeach()

        foreach(TREESHEETS_WINSDK_LIB_KIND um ucrt)
            set(TREESHEETS_WINSDK_LIB_DIR
                "${TREESHEETS_WINSDK_ROOT}/Lib/${TREESHEETS_WINSDK_VERSION}/${TREESHEETS_WINSDK_LIB_KIND}/${TREESHEETS_MSVC_ARCH}")
            if(EXISTS "${TREESHEETS_WINSDK_LIB_DIR}")
                link_directories("${TREESHEETS_WINSDK_LIB_DIR}")
            endif()
        endforeach()
    endif()
endif()

### Settings

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
OPTION(ENABLE_LOBSTER "Enable Lobster scripting" ON)
option(ENABLE_IPO "Enable interprocedural/link time optimization" ON)
if(ENABLE_IPO)
    if(MSVC AND ("$ENV{LIB}" STREQUAL "" OR "$ENV{INCLUDE}" STREQUAL ""))
        message(STATUS "Skipping IPO check because the MSVC environment is not initialized.")
    else()
        include(CheckIPOSupported)
        check_ipo_supported(RESULT iporesult OUTPUT output)
        if(iporesult)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "IPO is not supported: ${output}")
        endif()
    endif()
endif()

## Compiler-specific

# Use statically-linked libraries with MSVC
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
endif(MSVC)

# Silence warnings in GCC that contain lots of false positives
if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wno-array-bounds -Wno-stringop-overflow -Wno-maybe-uninitialized")
endif()

### Thirdparty dependencies

# Set wxWidgets options to exclude unused libraries

set(wxUSE_WEBVIEW FALSE)
set(wxUSE_STC FALSE)
set(wxUSE_RIBBON FALSE)
set(wxUSE_PROPGRID FALSE)
set(wxUSE_RICHTEXT FALSE)
set(wxUSE_MEDIACTRL FALSE)
if (NOT wxBUILD_SHARED)
    set(wxUSE_HTML FALSE)
    set(wxUSE_WXHTML_HELP FALSE)
endif()

# Include dependencies

include(FetchContent)

FetchContent_Declare(
    wxwidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.tar.bz2
    URL_HASH SHA256=50a28cb668de47b0e006cd6ebed8cf4f76c1cac6116fb3c978c44478219103f2
    FIND_PACKAGE_ARGS 3.3.2 NAMES wxWidgets
)
FetchContent_MakeAvailable(wxwidgets)

if(ENABLE_LOBSTER)
    FetchContent_Declare(
        lobster
        URL https://github.com/aardappel/lobster/archive/refs/tags/v2026.1.tar.gz
        URL_HASH SHA256=fecd443c8bf03052f0eca3308bb8428e1f40292d61fb370e060e55094f853d67
    )
    FetchContent_MakeAvailable(lobster)
endif()

### Options

## Run clang-tidy linter

OPTION(WITH_CLANG_TIDY "Run clang-tidy linter" OFF)
if (WITH_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=cppcoreguidelines-*,clang-analyzer-*,readability-*,performance-*,portability-*,concurrency-*,modernize-*)
endif()

### Libraries (pinyin, lobster, lobster-impl)

add_library(TreeSheetsPinyin STATIC
    thirdparty/pinyin/vendor/source/data.cpp
    thirdparty/pinyin/vendor/source/double_pinyin.cpp
    thirdparty/pinyin/vendor/source/pinyin.cpp)
target_include_directories(TreeSheetsPinyin PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/pinyin/vendor/include")
target_compile_definitions(TreeSheetsPinyin PUBLIC IB_PINYIN_ENCODING=32)
if(MSVC)
    target_compile_options(TreeSheetsPinyin PRIVATE /utf-8)
endif()

## lobster (script interpreter)
if(ENABLE_LOBSTER)
    add_library(lobster STATIC
        ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/idl_gen_text.cpp
        ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/idl_parser.cpp
        ${lobster_SOURCE_DIR}/dev/external/flatbuffers/src/util.cpp
        ${lobster_SOURCE_DIR}/dev/src/builtins.cpp
        ${lobster_SOURCE_DIR}/dev/src/compiler.cpp
        ${lobster_SOURCE_DIR}/dev/src/file.cpp
        ${lobster_SOURCE_DIR}/dev/src/lobsterreader.cpp
        ${lobster_SOURCE_DIR}/dev/src/platform.cpp
        ${lobster_SOURCE_DIR}/dev/src/vm.cpp
        ${lobster_SOURCE_DIR}/dev/src/vmdata.cpp
        ${lobster_SOURCE_DIR}/dev/src/tccbind.cpp
        ${lobster_SOURCE_DIR}/dev/external/libtcc/libtcc.c)
    if(WIN32)
        target_sources(lobster PRIVATE
            ${lobster_SOURCE_DIR}/dev/include/StackWalker/StackWalker.cpp
            ${lobster_SOURCE_DIR}/dev/include/StackWalker/StackWalkerHelpers.cpp)
    endif(WIN32)
    target_include_directories(lobster PUBLIC
        ${lobster_SOURCE_DIR}/dev/src
        ${lobster_SOURCE_DIR}/dev/include
        ${lobster_SOURCE_DIR}/dev/external
        ${lobster_SOURCE_DIR}/dev/external/libtcc)

    ## lobster-impl (provider of TreeSheets functions in lobster)

    add_library(lobster-impl STATIC src/lobster_impl.cpp)
    target_link_libraries(lobster-impl PRIVATE lobster)
endif(ENABLE_LOBSTER)

### TreeSheets executable

file(GLOB TREESHEETS_HEADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(TreeSheets
    src/main.cpp
    src/stdafx.cpp)
set_source_files_properties(src/main.cpp PROPERTIES
    OBJECT_DEPENDS "${TREESHEETS_HEADER_FILES}")

target_compile_definitions(TreeSheets PRIVATE "PACKAGE_VERSION=\"${TREESHEETS_VERSION}\"")
if(ENABLE_LOBSTER)
    target_compile_definitions(TreeSheets PRIVATE "ENABLE_LOBSTER=1")
endif(ENABLE_LOBSTER)

if(APPLE)
    set_target_properties(TreeSheets PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 Wouter van Oortmerssen and Tobias Predel. All rights reserved."
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.strlen.TreeSheets"
        MACOSX_BUNDLE_ICON_FILE "App.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/osx/Info.plist"
    )
elseif(WIN32)
    target_sources(TreeSheets PRIVATE
        platform/win/treesheets.rc)
    set_target_properties(TreeSheets PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

target_precompile_headers(TreeSheets PUBLIC src/stdafx.h)

## Link wxWidgets and lobster-impl into TreeSheets
if(ENABLE_LOBSTER)
    target_link_libraries(TreeSheets PRIVATE lobster-impl)
endif(ENABLE_LOBSTER)
target_link_libraries(TreeSheets PRIVATE TreeSheetsPinyin wx::aui wx::adv wx::core wx::xml wx::net)

### Tests

option(TREESHEETS_BUILD_TESTS "Build TreeSheets tests" ON)
if(TREESHEETS_BUILD_TESTS)
    include(CTest)
    set(TREESHEETS_TEST_MAIN_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
        ${TREESHEETS_HEADER_FILES})

    add_library(TreeSheetsTestSupport INTERFACE)
    target_compile_definitions(TreeSheetsTestSupport INTERFACE
        "TREESHEETS_TESTING=1"
        "PACKAGE_VERSION=\"test\""
        "TREESHEETS_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
    target_link_libraries(TreeSheetsTestSupport INTERFACE
        TreeSheetsPinyin wx::aui wx::adv wx::core wx::xml wx::net)

    function(add_treesheets_test target source)
        set_source_files_properties(${source}
            PROPERTIES OBJECT_DEPENDS "${TREESHEETS_TEST_MAIN_DEPENDS}")
        add_executable(${target}
            ${source}
            src/stdafx.cpp)
        target_link_libraries(${target} PRIVATE TreeSheetsTestSupport)
        if(MSVC)
            target_compile_options(${target} PRIVATE /utf-8)
        endif()
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    endfunction()

    add_treesheets_test(TreeSheetsTextTests tests/text_tests.cpp)
    add_treesheets_test(TreeSheetsGridTests tests/grid_tests.cpp)
    add_treesheets_test(TreeSheetsSerializationTests tests/serialization_tests.cpp)
    add_treesheets_test(TreeSheetsDocumentTests tests/document_tests.cpp)

    add_custom_target(TreeSheetsTests
        DEPENDS TreeSheetsTextTests TreeSheetsGridTests TreeSheetsSerializationTests TreeSheetsDocumentTests)
endif()

### Installation

## Platform specific installation paths

if(LINUX OR BSD)
    OPTION(TREESHEETS_RELOCATABLE_INSTALLATION "Install data relative to the TreeSheets binary, instead of respecting the Filesystem Hierarchy Standard" OFF)
endif()

if((LINUX OR BSD) AND NOT TREESHEETS_RELOCATABLE_INSTALLATION)
    include(GNUInstallDirs)

    set(TREESHEETS_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(TREESHEETS_DOCDIR ${CMAKE_INSTALL_DOCDIR})
    set(TREESHEETS_FULL_DOCDIR ${CMAKE_INSTALL_FULL_DOCDIR})
    set(TREESHEETS_PKGDATADIR ${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME})
    set(TREESHEETS_FULL_PKGDATADIR ${CMAKE_INSTALL_FULL_DATADIR}/${CMAKE_PROJECT_NAME})

    # Convert relative to absolute paths because only absolute paths are looked up on Linux (and BSD)
    target_compile_definitions(TreeSheets PRIVATE
        "LOCALEDIR=L\"${CMAKE_INSTALL_FULL_LOCALEDIR}\""
        "TREESHEETS_DOCDIR=\"${TREESHEETS_FULL_DOCDIR}\""
        "TREESHEETS_DATADIR=\"${TREESHEETS_FULL_PKGDATADIR}\""
    )

    # Adapt the AppStream metainfo to release version and date
    string(TIMESTAMP TREESHEETS_RELEASE_DATE "%Y-%m-%d")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/com.strlen.TreeSheets.metainfo.xml.in"
        "${CMAKE_CURRENT_BINARY_DIR}/com.strlen.TreeSheets.metainfo.xml"
        @ONLY
    )

    install(FILES platform/linux/com.strlen.TreeSheets.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES platform/linux/com.strlen.TreeSheets.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES platform/linux/com.strlen.TreeSheets.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/com.strlen.TreeSheets.metainfo.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
elseif(APPLE)
    # Paths must be relative to use with CPack
    set(TREESHEETS_BINDIR .)
    set(TREESHEETS_DOCDIR TreeSheets.app/Contents/Resources)
    set(TREESHEETS_PKGDATADIR TreeSheets.app/Contents/Resources)
else()
    set(TREESHEETS_BINDIR .)
    set(TREESHEETS_DOCDIR .)
    set(TREESHEETS_PKGDATADIR .)
endif()

## Installation

install(TARGETS TreeSheets DESTINATION ${TREESHEETS_BINDIR})
install(DIRECTORY TS/docs DESTINATION ${TREESHEETS_DOCDIR})
file(GLOB treesheets_readme_files "TS/readme*.html")
install(FILES ${treesheets_readme_files} DESTINATION ${TREESHEETS_DOCDIR})
install(DIRECTORY TS/examples DESTINATION ${TREESHEETS_DOCDIR})

install(DIRECTORY TS/images DESTINATION ${TREESHEETS_PKGDATADIR})
install(DIRECTORY TS/scripts DESTINATION ${TREESHEETS_PKGDATADIR})
if(ENABLE_LOBSTER)
    set(lobster_modules
        ${lobster_SOURCE_DIR}/modules/std.lobster
        ${lobster_SOURCE_DIR}/modules/stdtype.lobster
        ${lobster_SOURCE_DIR}/modules/vec.lobster
        ${lobster_SOURCE_DIR}/modules/color.lobster
    )
    install(FILES ${lobster_modules} DESTINATION ${TREESHEETS_PKGDATADIR}/scripts/modules)
endif(ENABLE_LOBSTER)

## Apple icon set
if(APPLE)
    install(
        FILES "platform/osx/App.icns"
        DESTINATION "TreeSheets.app/Contents/Resources"
    )
endif()

## Localization

# Install translations to correct platform-specific path.
# See: https://docs.wxwidgets.org/trunk/overview_i18n.html#overview_i18n_mofiles
file(GLOB mo_files "${CMAKE_CURRENT_SOURCE_DIR}/TS/translations/*/ts.mo")
foreach(mo_file IN LISTS mo_files)
    cmake_path(GET mo_file PARENT_PATH locale_dir)
    cmake_path(GET locale_dir FILENAME locale)

    if(WIN32 OR TREESHEETS_RELOCATABLE_INSTALLATION)
        # Paths must be relative to use with CPack
        install(FILES "${mo_file}" DESTINATION "translations/${locale}")
    elseif(APPLE)
        # Paths must be relative to use with CPack
        install(FILES "${mo_file}" DESTINATION "TreeSheets.app/Contents/Resources/translations/${locale}.lproj")
    else()
        # Falling back to GNU scheme
        install(FILES "${mo_file}" DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${locale}/LC_MESSAGES")
    endif()
endforeach()

### Packaging with CPack

set(CPACK_PACKAGE_VENDOR "Wouter van Oortmerssen")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "INNOSETUP" "ZIP")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CMAKE_PROJECT_NAME})
    set(CPACK_PACKAGE_EXECUTABLES "TreeSheets" "TreeSheets")
    if(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
        set(CPACK_INNOSETUP_SETUP_ArchitecturesAllowed "arm64")
        set(ARCH_NAME "winarm64")
    else()
        set(CPACK_INNOSETUP_SETUP_ArchitecturesAllowed "x64compatible")
        set(ARCH_NAME "winx64")
    endif()
    set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PROJECT_VERSION}-${ARCH_NAME}")
    set(CPACK_INNOSETUP_RUN_EXECUTABLES "TreeSheets")
    set(CPACK_INNOSETUP_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
    set(CPACK_INNOSETUP_LANGUAGES
        "brazilianPortuguese" "english" "french" "german" 
        "italian" "korean" "russian"
    )
    set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest")
    set(CPACK_INNOSETUP_IGNORE_README_PAGE ON)
    set(CPACK_INNOSETUP_IGNORE_LICENSE_PAGE ON)
    set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "contrib/text")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Wouter van Oortmerssen")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_EPOCH 2)
endif()
include(CPack)
