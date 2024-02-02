#include "tunnel_loggin.h"

#include <iostream>

TunnelLogging::Severity TunnelLogging::_min_severity = TunnelLogging::Severity::INFO;

void TunnelLogging::print(const std::string& msg, Severity sev)
{
  if(sev < _min_severity) return;

  std::string sev_str;
    
  switch(sev) {
  case Severity::VERBOSE: sev_str = "Verbose"; break;
  case Severity::INFO:    sev_str = "Info";    break;
  case Severity::WARNING: sev_str = "Warning"; break;
  case Severity::ERROR:   sev_str = "Error";   break;
  default: sev_str = "Unknown";
  }

  std::cout << "[Native Tunnel Client][Log-" << sev_str << "] : " << msg;
}
