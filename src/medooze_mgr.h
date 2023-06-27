#ifndef MEDOOZE_MGR_H
#define MEDOOZE_MGR_H

#include <condition_variable>

#include "websocket.h"

class MedoozeMgr
{
  WebSocketSecure _ws;

  std::condition_variable _cv;
  std::mutex              _cv_mutex;
  
public:

  using json = nlohmann::json;
  
  size_t probing_bitrate = 2000z;
  bool   probing = true;

  std::string csv_url;
  std::string host;

  int port;
  std::function<void(const json&)> onanswer;
  
public:
  
  MedoozeMgr();

  void start();
  void stop();
  void view(const std::string& sdp);
  int get_rtp_port();
  
};

#endif /* MEDOOZE_MGR_H */
