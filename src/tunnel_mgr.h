#ifndef TUNNEL_MGR_H
#define TUNNEL_MGR_H

#include <atomic>
#include <queue>
#include <condition_variable>

#include "medooze_mgr.h"
#include "peerconnection.h"

struct TunnelSocket
{
  using json = nlohmann::json;
  
  WebSocket socket;

  std::string host;
  int port;

  std::atomic_bool is_connected = false;
  std::condition_variable cv;
  std::mutex cv_mutex;
  
  int session_id;
  
  void connect();
  void disconnect();
  void send(std::string_view cmd, int req, const json& data);
};

struct Capabilities
{
  std::string              impl;
  bool                     datagrams;
  bool                     streams;
  std::vector<std::string> cc;
};

struct CapabititiesVector
{
  std::vector<Capabilities> caps;

  const std::string& operator[](size_t impl, size_t cc) { return caps[impl].cc[cc]; }
  std::tuple<const std::string&, bool, bool> operator[](size_t impl) {
    return { caps[impl].impl, caps[impl].datagrams, caps[impl].streams };
  }
  void from_json(const std::vector<nlohmann::json>& data);
};

class TunnelMgr
{
  using json = nlohmann::json;
  
  static constexpr int START_REQUEST = 0;
  static constexpr int STOP_REQUEST = 1;
  static constexpr int LINK_REQUEST = 2;
  static constexpr int OUT_REQUEST = 3;
  static constexpr int CAPABILITIES_REQUEST = 6;
  static constexpr int UPLOAD_REQUEST = 8;
  static constexpr int GETSTATS_REQUEST = 9;

  std::atomic_bool   _running;
  MedoozeMgr&        _medooze;
  PeerconnectionMgr& _pc;

  CapabititiesVector _caps;

  void parse_client_response(const json& response);
  void parse_server_response(const json& response);

  std::condition_variable _cv, _cv2;
  std::mutex _cv_mutex, _cv_mutex2;

  std::string curl_cmd;
  std::filesystem::path _result_path;
  
public:

  using Constraints = std::optional<std::tuple<int, int, int, int>>;

  struct
  {
    std::string impl;
    std::string cc;
    bool        datagrams;
    int         rtp_port;
    int         quic_port;
    std::string quic_host;
    bool        external_file_transfer;
  } in_config, out_config;

  TunnelSocket client;
  TunnelSocket server;

  std::string exp_name;

  std::function<void()> onstart;
  std::function<void()> onstop;
  std::function<void(/*caps*/)> oncapabilities;

  std::queue<Constraints> constraints;
  
  TunnelMgr(MedoozeMgr& m, PeerconnectionMgr& pc);
  ~TunnelMgr();

  void connect();
  void disconnect();

  void start();
  void stop();

  void run(std::queue<Constraints>& c);
  void run_all(int repet, std::queue<Constraints>& c);

  void query_capabilities();
  void get_stats();
  void upload_stats();

  void reset_link();
  void set_link(int bitrate, int delay, int loss);
};

#endif /* TUNNEL_MGR_H */
