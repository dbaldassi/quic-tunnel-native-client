cmake_minimum_required( VERSION 3.16 FATAL_ERROR )

project( quic-tunnel-native-client )

find_package( libwebrtc ${WEBRTC_API_VERSION}  )
message( STATUS "Found libwebrtc at ${libwebrtc_DIR}." )

find_package( X11 REQUIRED )

# --- ASIO
include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/external/asio/asio/include )

# --- websocketpp
include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/external/websocketpp )

# --- JSON
set( JSON_BuildTests      OFF CACHE INTERNAL "" )
set( JSON_Install         OFF CACHE INTERNAL "" )
set( JSON_MultipleHeaders ON  CACHE INTERNAL "" )
add_subdirectory( external/nlohmann_json )

# --- src
add_subdirectory( src )
