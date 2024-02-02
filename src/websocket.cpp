#include "websocket.h"

#include <sstream>

void WebSocket::connect(std::string_view host, int port, const std::string& protocol)
{
  // Create websocket url
  std::ostringstream url;
  url << "ws://" << host << ":" << port;

  WebSocketBase::connect(url.str(), protocol);
}

void WebSocket::connect(std::string_view host, std::string_view port, const std::string& protocol)
{
  std::ostringstream url;
  url << "ws://" << host << ":" << port;

  WebSocketBase::connect(url.str(), protocol);
}


void WebSocketSecure::connect(std::string_view host, int port, const std::string& protocol)
{
  // Create websocket url
  std::ostringstream url;
  url << "wss://" << host << ":" << port;

  WebSocketBase::connect(url.str(), protocol);
}

void WebSocketSecure::connect(std::string_view host, std::string_view port, const std::string& protocol)
{
  std::ostringstream url;
  url << "wss://" << host << ":" << port;

  WebSocketBase::connect(url.str(), protocol);
}

websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> WebSocketSecure::on_tls_init(websocketpp::connection_hdl hdl)
{
  auto ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::tlsv13_client);
  try {
    // Remove support for undesired TLS versions
    ctx->set_options(asio::ssl::context::default_workarounds |
                     asio::ssl::context::no_sslv2 |
                     asio::ssl::context::no_sslv3 |
                     asio::ssl::context::no_tlsv1 |
                     asio::ssl::context::single_dh_use);
  }
  catch (std::exception& e) {
    TUNNEL_LOG(TunnelLogging::Severity::ERROR) << "on tls init error";
  }

  return ctx;
}

