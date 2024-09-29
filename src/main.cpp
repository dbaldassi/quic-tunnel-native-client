#include <iostream>
#include <csignal>
#include <thread>
#include <cstdlib>
#include <unistd.h>

#include <gtk/gtk.h>

#include "tunnel_loggin.h"
#include "tunnel_mgr.h"
#include "main_wnd.h"

#define T(TIME,BITRATE,DELAY,LOSS) std::make_tuple(TIME,BITRATE,DELAY,LOSS)

namespace config
{

constexpr int medooze_port = 8084;
constexpr const char * medooze_host = "192.168.1.30";

// constexpr int quic_server_port = 49888;
constexpr int quic_server_port = 8888;
// constexpr int quic_server_port = 50084;

// constexpr const char * quic_server_host = "88.121.171.111";
constexpr const char * quic_server_host = "192.168.1.47";
// constexpr const char * quic_server_host = "192.168.1.30";

constexpr bool enable_medooze_bwe = true;
constexpr int medooze_probing = 2000;

constexpr const char * WS_CLIENT_HOST = "192.168.1.30";
constexpr const int WS_CLIENT_PORT = 3333;

constexpr const char * WS_SERVER_HOST = "192.168.1.47";
// constexpr const char * WS_SERVER_HOST = "192.168.1.30";
constexpr const int WS_SERVER_PORT = 3334;

}

int main(int argc, char *argv[])
{
  gtk_init(&argc, &argv);

#if !GLIB_CHECK_VERSION(2, 35, 0)
  g_type_init();
#endif
  // g_thread_init API is deprecated since glib 2.31.0, see release note:
  // http://mail.gnome.org/archives/gnome-announce-list/2011-October/msg00041.html
#if !GLIB_CHECK_VERSION(2, 31, 0)
  g_thread_init(NULL);
#endif

  // rtc::LogMessage::LogToDebug(rtc::LoggingSeverity::TunnelLogging::Severity::INFO);
  
  TunnelLogging::set_min_severity(TunnelLogging::Severity::INFO);
  
  MedoozeMgr        medooze;
  PeerconnectionMgr pc;
  
  medooze.host = config::medooze_host;
  medooze.port = config::medooze_port;
  medooze.probing = config::enable_medooze_bwe;
  medooze.probing_bitrate = config::medooze_probing;
  
  TunnelMgr tunnel(medooze, pc);
  tunnel.in_config.impl = "mvfst";
  tunnel.in_config.cc = "newreno";
  tunnel.in_config.datagrams = false;
  tunnel.in_config.quic_port = config::quic_server_port;
  tunnel.in_config.quic_host = config::quic_server_host;
  tunnel.in_config.external_file_transfer = false;

  tunnel.out_config = tunnel.in_config;
  tunnel.out_config.quic_port = 8888;
  // tunnel.out_config.impl = "quicgo";

  tunnel.client.host = config::WS_CLIENT_HOST;
  tunnel.client.port = config::WS_CLIENT_PORT;

  tunnel.server.host = config::WS_SERVER_HOST;
  tunnel.server.port = config::WS_SERVER_PORT;
  
  tunnel.connect();
  tunnel.query_capabilities();
  
  WindowRenderer window;
  auto res = window.create();

  if(!res) {
    TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "Could not create window";
    std::exit(EXIT_FAILURE);
  }

  pc.video_sink = &window;

  std::thread([](){ gtk_main(); }).detach();
  
  // std::deque<TunnelMgr::Constraints> constraints_init{T(60, 2500, 50, 5)};
  /*std::deque<TunnelMgr::Constraints> constraints_init{
    T(60, 2500, 0, 0), {}, T(60, 2500, 25, 0), {}, T(60, 2500, 50, 0), {}, T(60, 2500, 100, 0), {},
    T(60, 2500, 0, 1), {}, T(60, 2500, 25, 1), {}, T(60, 2500, 50, 1), {}, T(60, 2500, 100, 1), {},
    T(60, 2500, 0, 5), {}, T(60, 2500, 25, 5), {}, T(60, 2500, 50, 5), {}, T(60, 2500, 100, 5), {},
    T(60, 2500, 0, 10), {}, T(60, 2500, 25, 10), {}, T(60, 2500, 50, 10), {}, T(60, 2500, 100, 10),
    };*/
  std::deque<TunnelMgr::Constraints> constraints_init{T(30, 1000, 0, 0), T(30, 500, 0, 0), T(15, 1000, 0, 0), T(30, 2500, 0, 0), T(15, 1000, 0, 0)};
  // std::deque<TunnelMgr::Constraints> constraints_init{T(30, 2500, 0, 0), T(30, 2500, 0, 0), T(30, 2500, 0, 0), T(30, 2500, 0, 0), T(15, 1000, 0, 0)};
  // std::deque<TunnelMgr::Constraints> constraints_init{T(600, 8000, 0, 0), {}, T(600, 8000, 0, 0), {}, T(600, 8000, 0, 0), {}, T(600, 8000, 0, 0), {}, T(600, 8000, 0, 0), {}, T(600, 8000, 0, 0)};
  // std::deque<TunnelMgr::Constraints> constraints_init{T(300, 3000, 0, 0)};
  // std::deque<TunnelMgr::Constraints> constraints_init{T(30, 3000, 5, 0), {}, T(30, 3000, 10, 0), {}, T(30, 3000, 20, 0), {}, T(30, 3000, 30, 0), {}, T(30, 3000, 0, 0)};
  // std::deque<TunnelMgr::Constraints> constraints_init{T(30, 2500, 0, 0), T(30, 2500, 0, 5), T(30, 2500, 0, 10), T(30, 2500, 0, 20), T(30, 2500, 0, 30)};
  // std::deque<TunnelMgr::Constraints> constraints_init{T(30, 1000, 0, 0), T(30, 500, 0, 0), T(15, 1000, 0, 0), T(30, 2000, 0, 0), T(15, 1000, 0, 0)};
  // std::deque<TunnelMgr::Constraints> constraints_init{T(300, 3000, 0, 0)};
  std::queue<TunnelMgr::Constraints> constraints(constraints_init);
  
  // while(!constraints.empty()) {
  //   tunnel.start();
  //   tunnel.run(constraints);
  // }

  constexpr int repet = 5;
  tunnel.run_all(repet, constraints);
  
  tunnel.reset_link();
  std::this_thread::sleep_for(std::chrono::seconds{1});
    
  tunnel.disconnect();

  PeerconnectionMgr::clean();
  
  gtk_main_quit();
  window.destroy();
  
  return 0;
}
