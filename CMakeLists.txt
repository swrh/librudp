cmake_minimum_required(VERSION 2.8)
project(librudp C)

set(RUDP_MAJOR_VERSION 0)
set(RUDP_MINOR_VERSION 3)
set(RUDP_PATCH_VERSION 0)
set(RUDP_VERSION
  ${RUDP_MAJOR_VERSION}.${RUDP_MINOR_VERSION}.${RUDP_PATCH_VERSION})

# Offer the user the choice of overriding the installation directories
set(INSTALL_LIB_DIR lib CACHE PATH "Installation directory for libraries")
set(INSTALL_BIN_DIR bin CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDE_DIR include CACHE PATH
  "Installation directory for header files")
if(WIN32 AND NOT CYGWIN)
  set(DEF_INSTALL_CMAKE_DIR cmake)
else()
  set(DEF_INSTALL_CMAKE_DIR lib/cmake/librudp)
endif()
set(INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH
  "Installation directory for CMake files")

# Make relative paths absolute (needed later on)
foreach(p LIB BIN INCLUDE CMAKE)
  set(var INSTALL_${p}_DIR)
  if(NOT IS_ABSOLUTE "${${var}}")
    set(${var} "${CMAKE_INSTALL_PREFIX}/${${var}}")
  endif()
endforeach()

# set up include-directories
include_directories(
  "${PROJECT_SOURCE_DIR}"   # to find foo/foo.h
  "${PROJECT_BINARY_DIR}")  # to find foo/config.h

# Add sub-directories
add_subdirectory(src)

# The interesting stuff goes here
# ===============================

# Add all targets to the build-tree export set
export(TARGETS rudp
  FILE "${PROJECT_BINARY_DIR}/LibrudpTargets.cmake")

# Export the package for use from the build-tree
# (this registers the build-tree with a global CMake-registry)
export(PACKAGE librudp)

# Create the LibrudpConfig.cmake and LibrudpConfigVersion files
file(RELATIVE_PATH REL_INCLUDE_DIR "${INSTALL_CMAKE_DIR}"
   "${INSTALL_INCLUDE_DIR}")
# ... for the build tree
set(CONF_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")
configure_file(LibrudpConfig.cmake.in
  "${PROJECT_BINARY_DIR}/LibrudpConfig.cmake" @ONLY)
# ... for the install tree
set(CONF_INCLUDE_DIRS "\${RUDP_CMAKE_DIR}/${REL_INCLUDE_DIR}")
configure_file(LibrudpConfig.cmake.in
  "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/LibrudpConfig.cmake" @ONLY)
# ... for both
configure_file(LibrudpConfigVersion.cmake.in
  "${PROJECT_BINARY_DIR}/LibrudpConfigVersion.cmake" @ONLY)

# Install the LibrudpConfig.cmake and LibrudpConfigVersion.cmake
install(FILES
  "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/LibrudpConfig.cmake"
  "${PROJECT_BINARY_DIR}/LibrudpConfigVersion.cmake"
  DESTINATION "${INSTALL_CMAKE_DIR}" COMPONENT dev)

# Install the export set for use with the install-tree
install(EXPORT LibrudpTargets DESTINATION
  "${INSTALL_CMAKE_DIR}" COMPONENT dev)
