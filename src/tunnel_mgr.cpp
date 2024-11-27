#include <rtc_base/logging.h>

#include <iostream>
#include <sstream>
#include <chrono>
#include <ranges>
#include <algorithm>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <sys/wait.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

namespace fs = std::filesystem;

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

    if(c.impl == "udp") c.cc.push_back("none");

    caps.push_back(std::move(c));
  }
}

// tunnelmgr //////////////////////////////////////////////////////////////////

TunnelMgr::TunnelMgr(MedoozeMgr& m, PeerconnectionMgr& pc)
  : _medooze(m), _pc(pc)
{
  client.socket.onmessage = [this](auto&& msg) {
    if(msg["type"] == "error") TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "Client received error" << msg["data"]["message"];
    else if(msg["type"] == "response") parse_client_response(msg);
  };

  server.socket.onmessage = [this](auto&& msg) {
    if(msg["type"] == "error") TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "Server received error" << msg["data"]["message"];
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
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "client received : " << response.dump();
  
  int req = response["transId"].get<int>();

  auto data = response["data"];
  
  switch(req) {
  case START_REQUEST:
    client.session_id = data["id"].get<int>();
    if(onstart) onstart();
    _pc.start();
    _cv.notify_all();
    break;
  case STOP_REQUEST:
    _cv.notify_all();
    break;
  case CAPABILITIES_REQUEST:
    // capabilities affect
    _caps.from_json(data["in_impls"].get<std::vector<json>>());
    _cv.notify_all();
    // capabilities callback
    break;
  default:
      std::unreachable();
  }
}

void TunnelMgr::parse_server_response(const json& response)
{
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "server received : " << response.dump();
  int req = response["transId"].get<int>();

  auto data = response["data"];

  switch(req) {
  case START_REQUEST: {
    server.session_id = data["id"].get<int>();

    // start client
    json data = {
      { "impl", in_config.impl },
      { "datagrams", in_config.datagrams },
      { "cc", in_config.cc },
      { "quic_port", in_config.quic_port },
      { "quic_host", in_config.quic_host },
      { "external_file_transfer", in_config.external_file_transfer }
    };

    client.send("startclient", START_REQUEST, data);
    
    break;
  }
  case STOP_REQUEST:
    // get stats
    _cv2.notify_all();
    break;
  case UPLOAD_REQUEST:
    // nothing / ack
    break;
  case GETSTATS_REQUEST: {
    std::string url = data["url"].get<std::string>();
    auto pos = url.find("/", url.find("/") + 2);

    // remove https://host/
    _result_path = url.substr(pos + 1);
    _result_path.remove_filename();

    // fs::copy("bitstream.264", path / fs::path{"bitstream.264"});

    _cv2.notify_all();
    break;
  }
  case LINK_REQUEST:

    break;
  default:
    std::unreachable();
  }
}

void TunnelMgr::connect()
{
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "TunnelMgr::connect";

  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "connect client";
  client.connect();
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "connect server";
  server.connect();
  
  TUNNEL_LOG(TunnelLogging::Severity::INFO) << "TunnelMgr::connected";
}

void TunnelMgr::disconnect()
{
  TUNNEL_LOG(TunnelLogging::Severity::INFO) << "TunnelMgr::disconnect";
  client.disconnect();
  server.disconnect();
}

void TunnelMgr::start()
{
  TUNNEL_LOG(TunnelLogging::Severity::INFO) << "TunnelMgr::start";
  _running = true;
  _medooze.start();

  out_config.rtp_port = _medooze.get_rtp_port();
  
  // start server
  json data = {
    { "impl", out_config.impl },
    { "datagrams", out_config.datagrams },
    { "cc", out_config.cc },
    { "port_out", out_config.rtp_port },
    { "addr_out", _medooze.host },
    { "quic_port", out_config.quic_port },
    { "quic_host", out_config.quic_host },
    { "external_file_transfer", out_config.external_file_transfer }
  };

  server.send("startserver", START_REQUEST, data);

  std::unique_lock<std::mutex> lck(_cv_mutex);
  _cv.wait(lck);
}

void TunnelMgr::stop()
{
  TUNNEL_LOG(TunnelLogging::Severity::INFO) << "TunnelMgr::stop";
  _running = false;

  reset_link();
  
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
  
  std::string cmd = fmt::format("cd {} && zip upload.zip * && {}", _result_path.string(), curl_cmd);
  std::system(cmd.c_str());
}

void TunnelMgr::run(std::queue<Constraints>& c)
{
  TUNNEL_LOG(TunnelLogging::Severity::INFO) << "TunnelMgr::run";
  
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

void TunnelMgr::run_all(int repet, std::queue<Constraints>& c)
{
  std::queue<Constraints> save = c;

  TUNNEL_LOG(TunnelLogging::Severity::INFO) << "--- Running all implementations ---";
  
  for(int r = 0; r < repet; ++r) {
    TUNNEL_LOG(TunnelLogging::Severity::INFO) << "# Repet : " << (r + 1);
    
    for(size_t impl = 0; impl < _caps.caps.size(); ++impl) {
      auto [name, dgram, stream] = _caps[impl];
      out_config.impl = name;
      in_config.impl = name;

      if(name == "quiche") continue;

      TUNNEL_LOG(TunnelLogging::Severity::INFO) << "## Impl : " << name;
      
      for(int d = 0; d < 2; ++d) { 
	if((d == 0 && !dgram) || (d == 1 && !stream)) continue;
	out_config.datagrams = d == 0;
	in_config.datagrams = d == 0;

	TUNNEL_LOG(TunnelLogging::Severity::INFO) << "### Datagram ? : " << ((d == 0) ? "Yes" : "No");
		
	for(size_t cc = 0; cc < _caps.caps[impl].cc.size(); ++cc) {	  
	  out_config.cc = _caps[impl, cc];
	  in_config.cc = _caps[impl, cc];

	  TUNNEL_LOG(TunnelLogging::Severity::INFO) << "#### cc : " << out_config.cc;

	  c = save;
	
	  while(!c.empty()) {
	    start();
	    run(c);
	  }

	  if(out_config.datagrams) break;
	}
      }
    }
  }

  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "Finito";
}

void TunnelMgr::query_capabilities()
{
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "TunnelMgr::query_capabilities";
  json data = {
    { "out_requested", false },
    { "in_requested", true }
  };

  client.send("capabilities", CAPABILITIES_REQUEST, data);

  std::unique_lock<std::mutex> lck(_cv_mutex);
  _cv.wait(lck);
}

void TunnelMgr::get_stats()
{
  using namespace std::chrono;
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "TunnelMgr::getstats";
  
  std::ostringstream oss;

  std::time_t now = system_clock::to_time_t(system_clock::now());
  std::string date = std::ctime(&now);
  std::ranges::replace(date, ' ', '_');

  oss << out_config.impl << "_" << out_config.cc << "_" << ((out_config.datagrams) ? "dgram" : "stream") << "_"
      << date.substr(0, date.size() - 1) << (out_config.external_file_transfer ? "_scp" : "");

  json data = {
    { "exp_name", oss.str() },
    { "transport", ((out_config.impl == "tcp" || out_config.impl == "udp") ? out_config.impl : "quic") },
    { "medooze_dump_url", _medooze.csv_url }
  };

  curl_cmd = fmt::format("curl http://localhost:4455 -Ffile=@upload.zip -Fexp={} -Freliability={} -Fcc={} -Fimpl={}",
			 exp_name,
			 ((out_config.datagrams) ? "dgram" : "stream"),
			 out_config.cc,
			 out_config.impl);
  
  server.send("getstats", GETSTATS_REQUEST, data);
}

void TunnelMgr::upload_stats()
{
  namespace ranges = std::ranges;
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "TunnelMgr::upload_stats";

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
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "TunnelMgr::reset_link";
  server.send("link", LINK_REQUEST, json{});
}

void TunnelMgr::set_link(int bitrate, int delay, int loss)
{
  TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "TunnelMgr::set_link";
  json data = {
    { "bitrate", bitrate },
    { "delay", delay },
    { "loss", loss }
  };

  server.send("link", LINK_REQUEST, data);
}
