cmake_minimum_required(VERSION 2.8.12)
project(osvrcore)

if(CMAKE_VERSION VERSION_LESS 3.0.0)
    message(FATAL_ERROR "You must use CMake 3.0.0 or greater.")
endif()

if(ANDROID AND CMAKE_VERSION VERSION_LESS 3.1.0)
    message(FATAL_ERROR "You must use CMake 3.1.0 or greater to build for Android.")
endif()

if (NOT DEFINED CMAKE_MACOSX_RPATH)
    set(CMAKE_MACOSX_RPATH ON)
endif()

set(LOCAL_CMAKE_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake-local")
# Custom CMake modules from https://github.com/rpavlik/cmake-modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" "${LOCAL_CMAKE_MODULE_DIR}")

###
# Package Options
###
include(Version)
include(GetSubprojectStatus)

###
# Library ABI versions
###
set(OSVR_PLUGINKIT_ABI_VERSION 0)

###
# Configuration Options
###

# This included script is shared with (e.g. Android) super-builds.
include(osvrBuildOptions)

# Build options that aren't shared with super-builds
if(WIN32 OR ANDROID)
    option(Boost_USE_STATIC_LIBS "Build with Boost's static libraries?" ON)
endif()
if(MSVC)
    option(BUILD_IN_PARALLEL "Should we use the MSVC /mp option to compile in parallel? Usually faster, but sometimes makes build errors harder to fix." ON)
endif()

# Shouldn't be changed without being fully aware of the consequences.
set(OSVR_PLUGIN_IGNORE_SUFFIX ".manualload")

# Figure out what to do with osvr_json_to_c
if(CMAKE_CROSSCOMPILING OR ANDROID)
    # If you are cross-compiling, please pass in OSVR_JSON_TO_C_COMMAND to CMake
    set(OSVR_CROSSCOMPILING ON)
    set(BUILD_OSVR_JSON_TO_C OFF)
    if(OSVR_JSON_TO_C_COMMAND)
        set(HAVE_OSVR_JSON_TO_C ON)
        set(OSVR_JSON_TO_C_EXECUTABLE ${OSVR_JSON_TO_C_COMMAND})
    else()
        set(HAVE_OSVR_JSON_TO_C OFF)
    endif()
else()
    # Right now, always build if we build the server
    set(BUILD_OSVR_JSON_TO_C ${BUILD_SERVER})
    set(HAVE_OSVR_JSON_TO_C ${BUILD_SERVER})
endif()

set(BUILD_USBSERIALENUM ${BUILD_SERVER_PLUGINS})
set(BUILD_OPENCV_CAMERA_PLUGIN ${BUILD_SERVER_PLUGINS})
if(ANDROID)
    set(BUILD_USBSERIALENUM OFF)
    set(BUILD_OPENCV_CAMERA_PLUGIN OFF)
endif()
if(NOT BUILD_WITH_OPENCV)
    set(BUILD_OPENCV_CAMERA_PLUGIN OFF)
endif()

set(BUILD_JOINTCLIENTKIT OFF)
set(BUILD_ANALYSISPLUGINKIT OFF)
if(BUILD_CLIENT AND BUILD_SERVER)
    set(BUILD_JOINTCLIENTKIT ON)
    set(BUILD_ANALYSISPLUGINKIT ON)
endif()

set(BUILD_VIDEOTRACKER_PLUGIN OFF)
if(BUILD_SERVER_PLUGINS AND BUILD_WITH_OPENCV)
    if(WIN32)
        # On Win32, for best experience, enforce the use of the DirectShow capture library.
        # TODO fix this package so it finds things on MSYS2/MinGW64
        # - the lib is in /mingw64/lib
        # - qedit.h is in /mingw64/x86_64-w64-mingw32/include
        find_package(DirectShow QUIET)
        if(MINGW OR (DIRECTSHOW_QEDIT_INCLUDE_DIR AND DIRECTSHOW_STRMIIDS_LIBRARY))
            # MinGW may include the header/lib in such a way that the find module
            # can't find them
            set(BUILD_VIDEOTRACKER_PLUGIN ON)
        else()
            message(STATUS "Skipping the video-based tracker plugin: Requires the 'Microsoft Windows SDK Update for Windows Vista' or older <http://www.microsoft.com/en-us/download/details.aspx?id=23719>")
            if(NOT DIRECTSHOW_QEDIT_INCLUDE_DIR)
                message(STATUS " - DIRECTSHOW_QEDIT_INCLUDE_DIR not found: Couldn't find a qedit.h file declaring ISampleGrabber.")
            endif()
            if(NOT DIRECTSHOW_STRMIIDS_LIBRARY)
                message(STATUS " - DIRECTSHOW_STRMIIDS_LIBRARY not found: Couldn't find the strmiids.lib or libstrmiids.a library.")
            endif()
        endif()
    elseif(NOT ANDROID)
        # Non-Windows desktop platforms can use the experimental OpenCV capture.
        # TODO get alternate capture libraries, like libuvc, that let us be choosier about our input.
        set(BUILD_VIDEOTRACKER_PLUGIN ON)
    endif()
endif()

set(OSVR_COPY_LIBFUNC OFF)
if(BUILD_SERVER AND (WIN32 OR ANDROID))
    set(OSVR_COPY_LIBFUNC ON)
endif()

###
# CMake Modules
###

# CMake-stock modules
include(CTest)
include(CPack)
include(InstallRequiredSystemLibraries)

include(GNUInstallDirs)
if(NOT CMAKE_INSTALL_DOCDIR)
	set(CMAKE_INSTALL_DOCDIR ${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME})
endif()
set(EXTRA_SAMPLE_BINDIR ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/extra-sample-apps)
if(WIN32)
    # On Windows, install sample configs, etc. to a subdirectory of bin.
    set(OSVR_CONFIG_ROOT "${CMAKE_INSTALL_BINDIR}")

    # In a build tree, we use the generator expression to put it alongside the server.
    set(OSVR_CACHED_CONFIG_ROOT "$<TARGET_FILE_DIR:osvr_server>" CACHE INTERNAL "")
else()
    set(OSVR_CONFIG_ROOT "${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}")
    # In build tree, put in similar location as we would in the install
    set(OSVR_CACHED_CONFIG_ROOT "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}" CACHE INTERNAL "")
endif()

# Shared modules from rpavlik/cmake-modules

# Must be before MapImportedReleaseVariants
find_package(OpenGL)
find_package(SDL2)

include(MapImportedReleaseVariants)
include(EnableExtraCompilerWarnings)
include(CopyImportedTarget)
if(BUILD_IN_PARALLEL)
    include(MSVCMultipleProcessCompile)
endif()

include(SetDefaultBuildType)
if(ANDROID)
    set_default_build_type(Release)
else()
    set_default_build_type(RelWithDebInfo)
endif()

include(UseFolders)
use_folders()

include(PlatformDefinitions)
define_platform_macros(OSVR)

include(osvrGtest)

# Modules that get shipped with an installed SDK
include(osvrAddPlugin)
include(osvrConvertJson)
include(CMakeBoostHelper)

###
# Dependencies
###

if(MSVC)
    # Hints for Windows and MSVC, given the Chocolatey packages.
    if(NOT "$ENV{ChocolateyInstall}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{ChocolateyInstall}/lib" CHOCO_LIB)
        list(APPEND CMAKE_PREFIX_PATH "${CHOCO_LIB}/OpenCV/tools/opencv/build" "${CHOCO_LIB}/OpenCV.2.4.10.20150209/tools/opencv/build")
    endif()
    list(APPEND CMAKE_PREFIX_PATH "c:/OpenCV249")
endif()

if(BUILD_SERVER)
    find_package(libfunctionality REQUIRED)
endif()

set(OSVR_COPY_OPENCV OFF)
if(BUILD_WITH_OPENCV)
    find_package(OpenCV)
    if(NOT OpenCV_FOUND)
        message(FATAL_ERROR "BUILD_WITH_OPENCV is enabled, but OpenCV was not found! Either help CMake find OpenCV or disable BUILD_WITH_OPENCV.")
    endif()
    if(OpenCV_VERSION VERSION_LESS 2.4)
        # At least one 2.3 version (used in Precise) is missing a header that
        # breaks their Eigen compatibility header. This seemed like the easiest
        # fix.
        set(BUILD_OPENCV_CAMERA_PLUGIN OFF)
    endif()
    if(WIN32)
        set(OSVR_COPY_OPENCV ON)
    endif()
    if(ANDROID)
        include(OpenCVAndroid)
    endif()
    # Include dir specifically required by 3.0 and up, but missing from config.
    if(NOT OpenCV_VERSION VERSION_LESS 3.0.0)
        list(REMOVE_DUPLICATES OpenCV_INCLUDE_DIRS)
        set_target_properties(opencv_core PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${OpenCV_INCLUDE_DIRS}")
    endif()
endif()

find_package(Threads REQUIRED)

# This find module wraps both the cmake config file-having and config-file-lacking builds.
find_package(JsonCpp REQUIRED)

include(CompilerFeatures)

set(OPENCV_MODULES_USED)

if(BUILD_WITH_OPENCV)
    if(TARGET opencv_hal)
        # Only exists for versions around 3.0
        list(APPEND OPENCV_MODULES_USED opencv_hal)
    endif()
    list(APPEND OPENCV_MODULES_USED opencv_core)

    # Extra OpenCV components.
    # We set this up here so that if we need to copy DLLs, we can do it in the main
    # library builds where it's easier to get the right directory on Windows in a build tree.
    if(BUILD_OPENCV_CAMERA_PLUGIN)
        if(OpenCV_VERSION_MAJOR LESS 3)
            set(OPENCV_CAMERA_EXTRA_LIBS opencv_highgui)
        else()
            set(OPENCV_CAMERA_EXTRA_LIBS opencv_videoio)
        endif()
        list(APPEND OPENCV_MODULES_USED ${OPENCV_CAMERA_EXTRA_LIBS})
    endif()

    if(BUILD_VIDEOTRACKER_PLUGIN)
        set(VIDEOTRACKER_EXTRA_LIBS
            opencv_imgproc
            opencv_features2d
            opencv_calib3d
            opencv_highgui
            opencv_imgproc
            opencv_flann) # flann is a transitive dependency

        if(WIN32 AND NOT OpenCV_VERSION_MAJOR LESS 3)
            # for the tracker camera viewer to be able to save a capture.
            list(APPEND VIDEOTRACKER_EXTRA_LIBS opencv_imgcodecs)
        endif()
        list(APPEND OPENCV_MODULES_USED ${VIDEOTRACKER_EXTRA_LIBS})
    endif()

endif()

if(OPENCV_MODULES_USED)
    list(REMOVE_DUPLICATES OPENCV_MODULES_USED)
endif()

###
# Figure out what Boost compiled libraries we need.
###
set(NEED_BOOST_PROGRAM_OPTIONS OFF)
set(NEED_BOOST_FILESYSTEM OFF)
set(NEED_BOOST_LOCALE OFF)

# Assume for now we always need this
set(NEED_BOOST_THREAD ON)

if(BUILD_SERVER_APP OR BUILD_CLIENT_APPS OR BUILD_OSVR_JSON_TO_C)
    set(NEED_BOOST_PROGRAM_OPTIONS ON)
endif()

if(BUILD_SERVER)
    set(NEED_BOOST_FILESYSTEM ON)
endif()

if(WIN32 AND NOT OSVR_HAVE_CODECVT)
    # Needed for USB Serial Enum and Winsock error processing.
    # either native or Boost Locale - set up in BoostTargets.cmake
    set(NEED_BOOST_LOCALE ON)
    set(OSVR_CODECVT_LIBRARIES boost_locale)
endif()

# This script runs find_package(Boost) for the right components and creates
# interface targets for the requested libraries
include(BoostTargets)

add_library(osvr_codecvt INTERFACE)
if(WIN32)
    if(OSVR_HAVE_CODECVT)
        target_compile_definitions(osvr_codecvt INTERFACE OSVR_HAVE_CODECVT)
    else()
        target_link_libraries(osvr_codecvt INTERFACE boost_locale)
    endif()
endif()
###
# Set up build directories
###
# Set up shared destination directories to avoid Windows hassle with DLL search paths.
if(WIN32)
    set(OSVR_SHARED_LIBRARY_DIR "${CMAKE_INSTALL_BINDIR}")
    set(OSVR_PLUGINS_UNDER_BINDIR ON)
else()
    set(OSVR_SHARED_LIBRARY_DIR "${CMAKE_INSTALL_LIBDIR}")
    set(OSVR_PLUGINS_UNDER_BINDIR OFF)
endif()
set(OSVR_PLUGIN_SUBDIR "osvr-plugins-${OSVR_PLUGINKIT_ABI_VERSION}")
set(OSVR_PLUGIN_BASEDIR "${OSVR_SHARED_LIBRARY_DIR}")
set(OSVR_PLUGIN_DIR "${OSVR_SHARED_LIBRARY_DIR}/${OSVR_PLUGIN_SUBDIR}")

if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${OSVR_SHARED_LIBRARY_DIR}")
endif()
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
endif()


# We set this variable so that we can share functions like osvr_add_plugin
# between builds in this, the main source tree, and external builds using a config file.
set(OSVR_CACHED_PLUGIN_DIR "${OSVR_PLUGIN_DIR}" CACHE INTERNAL
    "The OSVR_PLUGIN_DIR variable for OSVR, for use in building and installing plugins" FORCE)

# Since alias targets only work for libraries, we use this method instead to
# share the osvr_convert_json script between the main tree and external config users.
if(NOT OSVR_CROSSCOMPILING)
    set(OSVR_JSON_TO_C_EXECUTABLE "osvr_json_to_c" CACHE INTERNAL
        "The target name for the osvr_json_to_c executable" FORCE)
endif()

###
# Building
###
# vendored dependencies
add_subdirectory(vendor)

# Set RPATH for dynamic library search.
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
if(NOT WIN32) # Have not set this up for Windows yet
    list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
    if("${isSystemDir}" STREQUAL "-1")
        if(APPLE)
            # Standard RPATH
            set(CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
            # Extra install RPATH for those binaries installed in the EXTRA_SAMPLE_BINDIR
            set(EXTRABINARIES_INSTALL_RPATH "@loader_path/../../../${CMAKE_INSTALL_LIBDIR}")
        else()
            # Standard RPATH
            set(CMAKE_INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
            # Extra install RPATH for those binaries installed in the EXTRA_SAMPLE_BINDIR
            set(EXTRABINARIES_INSTALL_RPATH "\$ORIGIN/../../../${CMAKE_INSTALL_LIBDIR}")
        endif()
    else() #Use default rpath if libs installed in a system dir
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    endif()
endif()
set(HEADER_BASE "${CMAKE_CURRENT_SOURCE_DIR}/inc")
set(BUILDTREE_HEADER_BASE "${CMAKE_CURRENT_BINARY_DIR}/src")
if(WIN32 AND NOT DEFINED CMAKE_DEBUG_POSTFIX)
    set(OSVR_SET_DEBUG_POSTFIX TRUE)
    set(CMAKE_DEBUG_POSTFIX "d")
endif()

# Development tools
add_subdirectory(devtools)

# Core libraries
add_subdirectory(src/osvr)

if(WIN32 AND OSVR_SET_DEBUG_POSTFIX)
    set(CMAKE_DEBUG_POSTFIX "")
endif()

# Apps
add_subdirectory(apps)
if(BUILD_SERVER_PLUGINS)
    add_subdirectory(plugins)
endif()
if(BUILD_TESTING)
    add_subdirectory(examples)
    add_subdirectory(tests)
endif()

###
# Setup some basic files to support editors
###
# still could use additional include paths (and defines?) added.

# CMake convention note:
# Yes, this is generating in the source tree (bad),
# but the output files are not version controlled (good) and are in fact
# in gitignore (good)

# .clang_complete - for vim and the completion package in Atom
configure_file(.clang_complete.in "${CMAKE_CURRENT_SOURCE_DIR}/.clang_complete" @ONLY)

# For the linter-clang package in Atom
configure_file(.linter-clang-flags.in "${CMAKE_CURRENT_SOURCE_DIR}/.linter-clang-flags" @ONLY)
configure_file(.linter-clang-includes.in "${CMAKE_CURRENT_SOURCE_DIR}/.linter-clang-includes" @ONLY)

###
# Install examples
###
install(DIRECTORY examples/
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/examples"
    COMPONENT DevelExamples)

###
# Install schemas
###
install(FILES
    json-schemas/device_descriptor_schema.json
    json-schemas/display_descriptor_schema_v1.json
    json-schemas/osvr_server_config_schema.json
    json-schemas/README.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/json-schemas"
    COMPONENT Doc)

###
# Build and install documentation
###
if(WIN32)
    set(README_DEST .)
else()
    set(README_DEST "${CMAKE_INSTALL_DOCDIR}")
endif()

# For generating documentation in HTML
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendor/discount-windows-bins")
endif()
find_package(Markdown)

set(README_MARKDOWN
    BINARY_README.md
    CONTRIBUTING.md
    README.md
    NEWS.md)

if(BUILD_WITH_TRACING)
    list(APPEND README_MARKDOWN "${ETWPROVIDERS_OSVR_README}")
endif()
if(MARKDOWN_FOUND)
    include(UseMarkdown)
    add_markdown_target(markdown-readme ${CMAKE_CURRENT_BINARY_DIR} ${README_MARKDOWN})
    install_markdown_target(markdown-readme DESTINATION "${README_DEST}" COMPONENT Runtime)
else()
    install(FILES ${README_MARKDOWN}
        DESTINATION "${README_DEST}"
        COMPONENT Runtime)
endif()
install(FILES LICENSE NOTICE
    DESTINATION "${README_DEST}"
    COMPONENT Runtime)

include(DoxygenTargets)
# Internal Doxygen (includes implementation details) - target "doc"
add_doxygen("${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"
    EXTRA_INPUT "${CMAKE_CURRENT_BINARY_DIR}/src"
    EXTRA_STRIP_FROM_PATH "${CMAKE_CURRENT_BINARY_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}"
    EXTRA_STRIP_FROM_INC_PATH "${CMAKE_CURRENT_BINARY_DIR}/src"
    NO_PDF)
# External Doxygen (only includes interfaces) - target "doc_external"
add_doxygen("${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile-external"
    EXTRA_STRIP_FROM_PATH "${CMAKE_CURRENT_SOURCE_DIR}"
    EXTRA_STRIP_FROM_INC_PATH "${CMAKE_CURRENT_BINARY_DIR}/src"
    OUTPUT_DIRECTORY docs-external
    DOC_TARGET doc_external
    INSTALL_DESTINATION "${CMAKE_INSTALL_DOCDIR}/api"
    INSTALL_COMPONENT DevelDocs
    NO_PDF)

# Replace some of the images from doxygen.
if(TARGET doc AND TARGET doc_external)
    foreach(fn ftv2mnode.png ftv2pnode.png ftv2mlastnode.png sync_on.png sync_off.png)
        add_custom_command(TARGET doc
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/doxygen/${fn}" "${CMAKE_CURRENT_BINARY_DIR}/docs-generated/html/"
            VERBATIM
            COMMENT "Copying image ${fn} to override Doxygen theme")

        add_custom_command(TARGET doc_external
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/doxygen/${fn}" "${CMAKE_CURRENT_BINARY_DIR}/docs-external/html/"
            VERBATIM
            COMMENT "Copying image ${fn} to override Doxygen theme")
    endforeach()
endif()

###
# Set Up Package for External Usage
###
include(CMakePackageConfigHelpers)
include(GenerateCompatibilityVersionFile)
set(PLUGINDIR ${OSVR_PLUGIN_DIR})
set(CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/osvr")

generate_compatibility_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/osvrConfigVersion.cmake"
    VERSION ${OSVR_VERSION}
    COMPATIBILITY AnyNewerVersion
    C_ABI)

generate_compatibility_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/osvrInternalConfigVersion.cmake"
    VERSION ${OSVR_VERSION}
    COMPATIBILITY AnyNewerVersion
    CXX_ABI)

# Be sure to export the json-to-c tool if we have built it.
if(TARGET osvr_json_to_c)
    list(APPEND OSVR_BUILDTREE_TARGETS osvr_json_to_c)
endif()

# Export the targets we want in the build tree.
# TODO exporting osvr_cxx11_flags means the build tree (only) is fairly compiler-specific
export(TARGETS
    ${OSVR_BUILDTREE_TARGETS}
    osvr_cxx11_flags
    FILE "${CMAKE_CURRENT_BINARY_DIR}/osvrTargets.cmake"
    NAMESPACE osvr::
)

# Register the current build dir as a package search location
export(PACKAGE osvr)

configure_file("${LOCAL_CMAKE_MODULE_DIR}/osvrConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/osvrConfig.cmake"
    @ONLY)

# Copy over the shared CMake modules for use in a build tree.
foreach(HELPER osvrAddPlugin osvrConvertJson CMakeBoostHelper)
    configure_file("${LOCAL_CMAKE_MODULE_DIR}/${HELPER}.cmake" "${CMAKE_CURRENT_BINARY_DIR}/${HELPER}.cmake" COPYONLY)
endforeach()

configure_file("${LOCAL_CMAKE_MODULE_DIR}/osvrConfigBuildTreePaths.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/osvrConfigBuildTreePaths.cmake"
    @ONLY)

if(WIN32)
    # Needed by CMakeBoostHelper
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GetMSVCVersion.cmake" "${CMAKE_CURRENT_BINARY_DIR}/GetMSVCVersion.cmake" COPYONLY)
endif()

install(EXPORT osvrTargets
    FILE
    osvrTargets.cmake
    NAMESPACE
    osvr::
    DESTINATION
    ${CONFIG_INSTALL_DIR}
    COMPONENT
    Devel)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/osvrConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/osvrConfigVersion.cmake"
    "${LOCAL_CMAKE_MODULE_DIR}/osvrAddPlugin.cmake"
    "${LOCAL_CMAKE_MODULE_DIR}/osvrConfigInstalledBoost.cmake"
    "${LOCAL_CMAKE_MODULE_DIR}/osvrConfigInstalledOpenCV.cmake"
    "${LOCAL_CMAKE_MODULE_DIR}/osvrConvertJson.cmake"
    DESTINATION
    ${CONFIG_INSTALL_DIR}
    COMPONENT
    Devel)

if(WIN32)
    # Windows-only helpers for CMake
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GetMSVCVersion.cmake"
        "${LOCAL_CMAKE_MODULE_DIR}/CMakeBoostHelper.cmake"
        DESTINATION
        ${CONFIG_INSTALL_DIR}
        COMPONENT
        Devel)
endif()
if(CMAKE_HOST_WIN32)
    # Scripts to register the SDK with CMake for easier building.
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/devtools/add_sdk_to_registry.cmd"
        "${CMAKE_CURRENT_SOURCE_DIR}/devtools/add_sdk_to_registry.ps1"
        DESTINATION .
        COMPONENT Devel)
endif()

if(ANDROID AND "${ANDROID_STL}" STREQUAL "gnustl_shared")
    if(__libstl) # TODO this is an internal variable in android-cmake!
        install(FILES
            "${__libstl}"
            DESTINATION
            ${OSVR_SHARED_LIBRARY_DIR}
            COMPONENT
            Runtime)
    endif()
endif()

###
# Install dependencies
###
#include(GetSearchDirs)
#get_search_dirs(libSearchDirs libfunctionality::functionality)
#set(APPS
#"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/osvr_server${CMAKE_EXECUTABLE_SUFFIX}")
#install(CODE "include(BundleUtilities)
#    fixup_bundle(\"${APPS}\" \"\" \"${libSearchDirs}\")")
