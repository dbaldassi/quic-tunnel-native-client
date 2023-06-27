#include <rtc_base/logging.h>

#include <iostream>
#include <sstream>
#include <chrono>
#include <ranges>
#include <algorithm>

#include "tunnel_mgr.h"

// tunnelsocket ///////////////////////////////////////////////////////////////

  
void TunnelSocket::connect()
{
  socket.onopen = [this]() { is_connected = true; cv.notify_all(); };
  socket.onclose = [this]() { cv.notify_all(); };

  socket.connect(host, port);

  std::unique_lock<std::mutex> lck(cv_mutex);
  cv.wait(lck);
}

void TunnelSocket::disconnect()
{
  socket.disconnect();
}
  
void TunnelSocket::send(std::string_view cmd, int req, const json& data)
{
  json payload = {
    { "cmd", cmd },
    { "transId", req },
    { "data", data }
  };

  socket.send(payload.dump());
}

// Capabititiesvector /////////////////////////////////////////////////////////

void CapabititiesVector::from_json(const std::vector<nlohmann::json>& data)
{
  for(auto jcap : data) {
    Capabilities c;
    c.impl = jcap["impl"].get<std::string>();
    c.datagrams = jcap["datagrams"].get<bool>();
    c.streams = jcap["streams"].get<bool>();
      
    auto cc = jcap["cc"].get<std::vector<std::string>>();
    c.cc = std::move(cc);

    caps.push_back(std::move(c));
  }
}

// tunnelmgr //////////////////////////////////////////////////////////////////

TunnelMgr::TunnelMgr(MedoozeMgr& m, PeerconnectionMgr& pc)
  : _medooze(m), _pc(pc)
{
  client.socket.onmessage = [this](auto&& msg) {
    if(msg["type"] == "error") RTC_LOG(LS_ERROR) << "Client received error" << msg["data"]["message"];
    else if(msg["type"] == "response") parse_client_response(msg);
  };

  server.socket.onmessage = [this](auto&& msg) {
    if(msg["type"] == "error") RTC_LOG(LS_ERROR) << "Server received error" << msg["data"]["message"];
    else if(msg["type"] == "response") parse_server_response(msg);
  };

  _pc.onlocaldesc = [this](auto&& desc) -> void { _medooze.view(desc); };
  _medooze.onanswer = [this](auto&& sdp) -> void { _pc.set_remote_description(sdp); };
}

TunnelMgr::~TunnelMgr()
{

}

void TunnelMgr::parse_client_response(const json& response)
{
  std::cout << "client received : " << response.dump() << "\n";
  
  int req = response["transId"].get<int>();

  auto data = response["data"];
  
  switch(req) {
  case START_REQUEST:
    client.session_id = data["id"].get<int>();
    if(onstart) onstart();
    _pc.start();
    _cv.notify_all();
    // client_stopped = false ?
    break;
  case STOP_REQUEST:
    _cv.notify_all();
    break;
  case CAPABILITIES_REQUEST:
    // capabilities affect
    _caps.from_json(data["in_impls"].get<std::vector<json>>());
    
    // capabilities callback
    break;
  default:
      std::unreachable();
  }
}

void TunnelMgr::parse_server_response(const json& response)
{
  std::cout << "server received : " << response.dump() << "\n";
  int req = response["transId"].get<int>();

  auto data = response["data"];

  switch(req) {
  case START_REQUEST: {
    server.session_id = data["id"].get<int>();

    // start client
    json data = {
      { "impl", config.impl },
      { "datagrams", config.datagrams },
      { "cc", config.cc },
      { "quic_port", config.quic_port },
      { "quic_host", config.quic_host },
      { "external_file_transfer", config.external_file_transfer }
    };

    client.send("startclient", START_REQUEST, data);
    
    // server_stopped = false ??
    break;
  }
  case STOP_REQUEST:
    // get stats
    _cv2.notify_all();
    break;
  case UPLOAD_REQUEST:
    // nothing / ack
    break;
  case GETSTATS_REQUEST:
    _cv2.notify_all();
    break;
  case LINK_REQUEST:

    break;
  default:
    std::unreachable();
  }
}

void TunnelMgr::connect()
{
  std::cout << "TunnelMgr::connect" << std::endl;

  std::cout << "connect client" << std::endl;
  client.connect();
  std::cout << "connect server" << std::endl;
  server.connect();
  
  std::cout << "TunnelMgr::connect end" << std::endl;
}

void TunnelMgr::disconnect()
{
  std::cout << "TunnelMgr::disconnect" << std::endl;
  client.disconnect();
  server.disconnect();
}

void TunnelMgr::start()
{
  std::cout << "TunnelMgr::start" << std::endl;
  _running = true;
  _medooze.start();

  config.rtp_port = _medooze.get_rtp_port();
  
  // start server
  json data = {
    { "impl", config.impl },
    { "datagrams", config.datagrams },
    { "cc", config.cc },
    { "port_out", config.rtp_port },
    { "addr_out", _medooze.host },
    { "quic_port", config.quic_port },
    { "quic_host", config.quic_host },
    { "external_file_transfer", config.external_file_transfer }
  };

  server.send("startserver", START_REQUEST, data);

  std::unique_lock<std::mutex> lck(_cv_mutex);
  _cv.wait(lck);
}

void TunnelMgr::stop()
{
  std::cout << "TunnelMgr::stop" << std::endl;
  _running = false;

  upload_stats();

  _medooze.stop();
  _pc.stop();

  // stop client
  auto client_th = std::thread([this]() {
    json data = { { "id", client.session_id } };
    client.send("stopclient", STOP_REQUEST, data);

    std::unique_lock<std::mutex> lck(_cv_mutex);
    _cv.wait(lck);
  });

  // stop server
  auto server_th = std::thread([this]() {
    json data = { { "id", server.session_id } };
    server.send("stopserver", STOP_REQUEST, data);

    std::unique_lock<std::mutex> lck(_cv_mutex2);
    _cv2.wait(lck);
  });

  if(client_th.joinable()) client_th.join();
  if(server_th.joinable()) server_th.join();

  get_stats();
  
  std::unique_lock<std::mutex> lck(_cv_mutex2);
  _cv2.wait(lck);
}

void TunnelMgr::run(std::queue<Constraints>& c)
{
  std::cout << "TunnelMgr::run" << std::endl;
  if(!_running) return;

  while(!c.empty()) {
    if(!c.front().has_value()) {
      c.pop();
      break;
    }

    auto [ time, bitrate, delay, loss ] = *c.front();
    set_link(bitrate, delay, loss);
    _pc.link = bitrate;

    c.pop();
    
    std::this_thread::sleep_for(std::chrono::seconds(time));
  }

  stop();
}

void TunnelMgr::query_capabilities()
{
  std::cout << "TunnelMgr::query_capabilities" << std::endl;
  json data = {
    { "out_requested", false },
    { "in_requested", true }
  };

  client.send("capabilities", CAPABILITIES_REQUEST, data);
}

void TunnelMgr::get_stats()
{
  using namespace std::chrono;
  std::cout << "TunnelMgr::getstats" << std::endl;
  
  std::ostringstream oss;

  std::time_t now = system_clock::to_time_t(system_clock::now());
  std::string date = std::ctime(&now);
  std::ranges::replace(date, ' ', '_');

  oss << config.impl << "_" << config.cc << "_" << ((config.datagrams) ? "dgram" : "stream") << "_"
      << date.substr(0, date.size() - 1) << (config.external_file_transfer ? "_scp" : "");

  json data = {
    { "exp_name", oss.str() },
    { "transport", ((config.impl == "tcp" || config.impl == "udp") ? config.impl : "quic") },
    { "medooze_dump_url", _medooze.csv_url }
  };

  server.send("getstats", GETSTATS_REQUEST, data);
}

void TunnelMgr::upload_stats()
{
  namespace ranges = std::ranges;
  std::cout << "TunnelMgr::upload_stats" << std::endl;

  std::vector<json> stats_data;
  
  ranges::transform(_pc.stats, std::back_inserter(stats_data), [](const auto& s) -> json {
    return json{
      { "x", s.x },
      { "bitrate", s.bitrate },
      { "link", s.link },
      { "fps", s.fps },
      { "frameDropped", s.frame_dropped },
      { "frameDecoded", s.frame_decoded },
      { "keyFrameDecoded", s.frame_key_decoded },
      { "frameRendered", 0 },
    };
  });

  json data = { { "stats",  stats_data } };

  server.send("uploadstats", UPLOAD_REQUEST, data);
}

void TunnelMgr::reset_link()
{
  std::cout << "TunnelMgr::reset_link" << std::endl;
  server.send("link", LINK_REQUEST, json{});
}

void TunnelMgr::set_link(int bitrate, int delay, int loss)
{
  std::cout << "TunnelMgr::set_link" << std::endl;
  json data = {
    { "bitrate", bitrate },
    { "delay", delay },
    { "loss", loss }
  };

  server.send("link", LINK_REQUEST, data);
}
