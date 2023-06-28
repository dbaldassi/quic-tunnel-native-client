
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
  main_wnd.h
  main_wnd.cpp
  )

target_include_directories( qclient PRIVATE
  ${CAIRO_INCLUDE_DIR}
  ${GTK_INCLUDE_DIRS}
  )

message(STATUS "${GTK_CFLAGS_OTHER}")

target_link_libraries( qclient PRIVATE
  ${WEBRTC_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${GTK_LIBRARIES}
  nlohmann_json
  )

target_compile_options( qclient PRIVATE ${GTK_CFLAGS_OTHER} )

