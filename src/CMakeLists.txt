# Winsock.
if(WIN32)
    set(CMAKE_EXTRA_INCLUDE_FILES winsock2.h ws2tcpip.h)
    set(CMAKE_REQUIRED_LIBRARIES  ws2_32.lib)
    set(CMAKE_REQUIRED_DEFINITIONS -FIwinsock2.h -FIws2tcpip.h)
endif()

set(SRC_CORE
    address.c
    client.c
    ela.c
    ela_libevent.c
    endpoint.c
    packet.c
    peer.c
    rudp.c
    server.c
    )

set(HDR_PRIVATE
    rudp_error.h
    rudp_list.h
    rudp_packet.h
    rudp_rudp.h
    )

include_directories(../include)

set(HDR_PUBLIC
    ../include/rudp/address.h
    ../include/rudp/client.h
    ../include/rudp/compiler.h
    ../include/rudp/ela_backend.h
    ../include/rudp/ela_ela.h
    ../include/rudp/ela_libevent.h
    ../include/rudp/endpoint.h
    ../include/rudp/error.h
    ../include/rudp/list.h
    ../include/rudp/packet.h
    ../include/rudp/peer.h
    ../include/rudp/rudp.h
    ../include/rudp/server.h
    ../include/rudp/time.h
    )

source_group("Headers Private"  FILES ${HDR_PRIVATE})
source_group("Headers Public"   FILES ${HDR_PUBLIC})
source_group("Source Core"      FILES ${SRC_CORE})

add_library(rudp SHARED
    ${HDR_PRIVATE}
    ${HDR_PUBLIC}
    ${SRC_CORE}
    )

set_target_properties(rudp PROPERTIES PUBLIC_HEADER "${HDR_PUBLIC}")

install(TARGETS rudp
  # IMPORTANT: Add the rudp library to the "export-set"
  EXPORT LibrudpTargets
  RUNTIME DESTINATION "${INSTALL_BIN_DIR}" COMPONENT bin
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}" COMPONENT shlib
  PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/rudp"
    COMPONENT dev)
