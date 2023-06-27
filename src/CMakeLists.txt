
add_executable( qclient )

target_compile_options( qclient PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-null-pointer-subtraction -fexperimental-library 
  -fmodules
  )

set_target_properties( qclient PROPERTIES CXX_STANDARD 23 )

target_sources( qclient PRIVATE
  main.cpp
  peerconnection.cpp
  peerconnection.h
  websocket.cpp
  websocket.h
  medooze_mgr.cpp
  medooze_mgr.h
  tunnel_mgr.cpp
  tunnel_mgr.h
  )

target_link_libraries( qclient PRIVATE
  ${WEBRTC_LIBRARIES}
  nlohmann_json
  )

