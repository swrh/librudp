cmake_minimum_required(VERSION 2.8)
project(librudp C)

set(RUDP_MAJOR_VERSION 0)
set(RUDP_MINOR_VERSION 3)
#set(RUDP_PATCH_VERSION 1)
set(RUDP_VERSION ${RUDP_MAJOR_VERSION}.${RUDP_MINOR_VERSION})

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

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/")
include(LibFindMacros)

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

# Winsock.
if(WIN32)
    set(CMAKE_EXTRA_INCLUDE_FILES winsock2.h ws2tcpip.h)
    set(CMAKE_REQUIRED_LIBRARIES  ws2_32.lib)
    set(CMAKE_REQUIRED_DEFINITIONS -FIwinsock2.h -FIws2tcpip.h)
endif()

set (RUDP__HAVE_LIBEVENT 0)
find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(LIBEVENT libevent>=2.0.4)
    set(RUDP__HAVE_LIBEVENT ${LIBEVENT_FOUND})
endif()

if (NOT RUDP__HAVE_LIBEVENT)
    find_package(Libevent REQUIRED)
    set (RUDP__HAVE_LIBEVENT LIBEVENT_FOUND)
endif()

if (NOT RUDP__HAVE_LIBEVENT)
    message(FATAL_ERROR "package 'libevent' not found")
endif()

message("libevent include: ${LIBEVENT_INCLUDE_DIRS}")
message("libevent lib: ${LIBEVENT_LIBRARIES}")

link_directories(${LIBEVENT_LIBRARY_DIRS})
include_directories(${LIBEVENT_INCLUDE_DIRS})

set(SRC_CORE
    src/address.c
    src/client.c
    src/ela.c
    src/ela_libevent.c
    src/endpoint.c
    src/packet.c
    src/peer.c
    src/rudp.c
    src/server.c
    )

set(HDR_PRIVATE
    src/rudp_error.h
    src/rudp_list.h
    src/rudp_packet.h
    src/rudp_rudp.h
    )

include_directories(include)

set(HDR_PUBLIC
    include/rudp/address.h
    include/rudp/client.h
    include/rudp/compiler.h
    include/rudp/ela_backend.h
    include/rudp/ela_ela.h
    include/rudp/ela_libevent.h
    include/rudp/endpoint.h
    include/rudp/error.h
    include/rudp/list.h
    include/rudp/packet.h
    include/rudp/peer.h
    include/rudp/rudp.h
    include/rudp/server.h
    include/rudp/time.h
    )

source_group("Headers Private"  FILES ${HDR_PRIVATE})
source_group("Headers Public"   FILES ${HDR_PUBLIC})
source_group("Source Core"      FILES ${SRC_CORE})

add_library(rudp SHARED
    ${HDR_PRIVATE}
    ${HDR_PUBLIC}
    ${SRC_CORE}
    )

target_link_libraries(rudp ${LIBEVENT_LIBRARIES})

set_target_properties(rudp PROPERTIES PUBLIC_HEADER "${HDR_PUBLIC}")

install(TARGETS rudp
  # IMPORTANT: Add the rudp library to the "export-set"
  EXPORT LibrudpTargets
  RUNTIME DESTINATION "${INSTALL_BIN_DIR}" COMPONENT bin
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}" COMPONENT shlib
  PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/rudp"
    COMPONENT dev)

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
