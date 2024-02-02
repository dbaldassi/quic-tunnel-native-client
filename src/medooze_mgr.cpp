#include <iostream>
#include "medooze_mgr.h"

#include "tunnel_loggin.h"

MedoozeMgr::MedoozeMgr()
{
  _ws.onopen = []() { TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "medooze ws opened"; };
  _ws.onclose = []() { TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "medooze ws closed"; };
}

void MedoozeMgr::start()
{
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "Start connection medooze manager";
  
  _ws.connect(host, port , "quic-relay-loopback");
  _ws.onmessage = [this](auto&& msg) {
    if(auto answer = msg.find("answer"); answer != msg.end()) {
      if(onanswer) onanswer(*answer);
    }
    if(auto url = msg.find("url"); url != msg.end()) {
      csv_url = url->template get<std::string>();
    }
  };
}

void MedoozeMgr::stop()
{
  _ws.disconnect();
}

void MedoozeMgr::view(const std::string& sdp)
{
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "MedoozeManager::view";
  
  json cmd = {
    { "cmd", "view" },
    { "offer", sdp },
    { "probing", ((probing) ? probing_bitrate : 0) },
    { "constant_probing", probing_bitrate }
  };

  _ws.send(cmd.dump());
}

int MedoozeMgr::get_rtp_port()
{
  int rtp_port;
  WebSocketSecure ws;
  
  ws.onmessage = [this, &rtp_port, &ws](auto&& msg) {
    if(auto port = msg.find("port"); port != msg.end()) {
      rtp_port = port->template get<int>();
    }

    ws.disconnect();
    _cv.notify_all();
  };

  ws.onclose = []() { ; };
  ws.connect(host, port, "port");

  std::unique_lock<std::mutex> lck(_cv_mutex);
  _cv.wait(lck);
  
  return rtp_port;
}
