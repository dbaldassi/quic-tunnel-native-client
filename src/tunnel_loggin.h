#ifndef TUNNEL_LOGGIN_H
#define TUNNEL_LOGGIN_H

#include <string>
#include <inttypes.h>
#include <sstream>

class TunnelLogging
{
  
public:

  enum class Severity : uint8_t { VERBOSE, INFO, WARNING, ERROR };

  static void print(const std::string& msg, Severity sev);

  static void set_min_severity(Severity sev)
  {
    _min_severity = sev;
  }

private:
  static Severity _min_severity;
};

template<TunnelLogging::Severity S>
class TunnelLoggingStream final
{
  std::ostringstream oss;
  
public:
  template<typename T>
  TunnelLoggingStream& operator<<(T&& arg) {
    oss << arg;
    return *this;
  }


  void call() {
    oss << "\n";
    TunnelLogging::print(oss.str(), S);
  }
};

template<TunnelLogging::Severity S>
class TunnelLoggingCall
{
public:
  void operator&(TunnelLoggingStream<S>& s) {
    s.call();
  }
};

#define TUNNEL_LOG(SEVERITY) TunnelLoggingCall<SEVERITY>() & TunnelLoggingStream<SEVERITY>()

#endif /* TUNNEL_LOGGIN_H */
