#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <functional>
#include <string_view>

#include "tunnel_loggin.h"

#define ASIO_STANDALONE
#define _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CPP11_THREAD_
#define _WEBSOCKETPP_CPP11_FUNCTIONAL_
#define _WEBSOCKETPP_CPP11_SYSTEM_ERROR_
#define _WEBSOCKETPP_CPP11_RANDOM_DEVICE_
#define _WEBSOCKETPP_CPP11_MEMORY_

// External Websocketpp headers
#include "websocketpp/config/asio_client.hpp"
#include "websocketpp/client.hpp"

#include "nlohmann/json.hpp"

using WssClient = websocketpp::client<websocketpp::config::asio_tls_client>;
using WsClient = websocketpp::client<websocketpp::config::asio_client>;
using MessagePtr = websocketpp::config::asio_client::message_type::ptr;

template<typename Client>
class WebSocketBase
{
protected:
  using json = nlohmann::json;
  Client                          _client;
  typename Client::connection_ptr _connection;
  std::thread                     _thread;
  std::mutex                      _mutex; // To manage access to send commands.
  std::mutex                      _close_mutex; // To manage access to close status.
  std::atomic_bool                _is_closed{true};

  void on_message(websocketpp::connection_hdl hdl, MessagePtr frame)
  {
    auto msg = json::parse(frame->get_payload());
    if(onmessage) onmessage(msg);
  }
  
  void on_opened(websocketpp::connection_hdl hdl)
  {
    TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "ws on open";
    _is_closed = false;
    if(onopen) onopen();
  }
  
  void on_closed(websocketpp::connection_hdl hdl)
  {
    TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "ws on close";
    if(onclose) onclose();
  }
  
public:
  // std::map<std::string, std::function<void(const json& data)>> on_message_callback;
  std::function<void()>            onopen;
  std::function<void()>            onclose;
  std::function<void(const json&)> onmessage;

  WebSocketBase()
  {
    _client.set_access_channels(websocketpp::log::alevel::none);
    _client.clear_access_channels(websocketpp::log::alevel::all);
    _client.set_error_channels(websocketpp::log::elevel::all);
    _client.init_asio();
  }
  ~WebSocketBase() { disconnect(); }

  void connect(const std::string& url, const std::string& protocol)
  {
    TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "Websocket::connect : " << url << " " << protocol;
    websocketpp::lib::error_code ec;

    try {   
      _connection = _client.get_connection(url, ec);
      if (!_connection) {
	TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "No WebSocket connection";
	return;
      }

      std::unique_lock<std::mutex> lock(_mutex);
      _connection->set_close_handshake_timeout(5000);
      if (ec) {
	TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "Error establishing websocket connection: " << ec.message();
	return;
      }

      _connection->set_message_handler([this](auto&& hdl, auto&& frame) { on_message(hdl, frame); });
      _connection->set_open_handler([this](auto&& hdl) { on_opened(hdl); });
      _connection->set_close_handler([this](auto&& hdl) { on_closed(hdl); });
      _connection->set_fail_handler([](auto&& hdl) -> void {});
      _connection->set_http_handler([](auto&&) {});

      if(!protocol.empty()) _connection->add_subprotocol(protocol);
    
      _client.connect(_connection);

      _thread = std::thread([this]() { _client.run(); });
    }
    catch (const std::exception& e) {
      TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "Connect exception: " <<  e.what();
      return;
    }
  }
  
  
  void disconnect()
  {
    std::unique_lock<std::mutex> lock(_mutex);
    TUNNEL_LOG(TunnelLogging::Severity::VERBOSE) << "Websocket::disconnect";
  
    if (!_connection) return;

    websocketpp::lib::error_code ec;
    try {

      std::unique_lock<std::mutex> close_lock(_close_mutex);
      if (!_is_closed) {
	_client.close(_connection, websocketpp::close::status::going_away, std::string{"disconnect"}, ec);
	_is_closed = true;
	if (ec) {
	  TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "Error on disconnect close: " << ec.message();
	}
      }
      close_lock.unlock();

      _thread.join();
      _client.reset();  
      _connection = nullptr;
    }
    catch (const std::exception& e) {
      TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "Disconnect exception: " << e.what();
    }
  }

  template<typename... Args>
  auto send(Args&& ... args) {
    std::unique_lock<std::mutex> lock(_mutex);
    if (!_connection) {
      throw std::runtime_error("Connection is null");
    }
    return _connection->send(std::forward<Args>(args)...);
  }
};

class WebSocketSecure : public WebSocketBase<WssClient>
{
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> on_tls_init(websocketpp::connection_hdl hdl);
  
public:
  WebSocketSecure() {
    _client.set_tls_init_handler([this](auto&& hdl) { return on_tls_init(hdl); });
  }
  
  void connect(std::string_view host, int port, const std::string& protocol = "");
  void connect(std::string_view host, std::string_view port, const std::string& protocol);
};

class WebSocket : public WebSocketBase<WsClient>
{

public:
  void connect(std::string_view host, int port, const std::string& protocol = "");
  void connect(std::string_view host, std::string_view port, const std::string& protocol);
};

#endif /* WEBSOCKET_H */
