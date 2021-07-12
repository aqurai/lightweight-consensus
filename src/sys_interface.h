
/* 
 * File:   sys_interface.h
 * Author: anwar
 *
 * Wrapper class to provide access to time and printing to console.
 */

#ifndef SYS_INTERFACE_H
#define SYS_INTERFACE_H

#include <chrono>
#include <stdint.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <sstream>
#include <iostream>


const double kSecondsToMicros = 1000000.0f;
class ReplicatedLog;

class SystemInterface {
public:
  friend class ReplicatedLog;
  friend class ConsensusManager;
  uint8_t self_id;
  
  double getTimeBootUs();
  void setSyncTime(std::chrono::time_point<std::chrono::high_resolution_clock> tp);
  void printConsole(const char* s, ...);

private:
  SystemInterface(uint8_t id);
  void printf_va( const char* s, va_list va);
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
  std::string color_code_;
};

#endif /* SYS_INTERFACE_H */

