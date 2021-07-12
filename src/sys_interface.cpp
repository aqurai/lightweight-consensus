
#include "sys_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <sstream>
#include <iostream>
#include <mutex>


// cout << "\033[1;31mbold red text\033[0m\n";
//          foreground background
// black        30         40
// red          31         41
// green        32         42
// yellow       33         43
// blue         34         44
// magenta      35         45
// cyan         36         46
// white        37         47


void SystemInterface::printConsole(const char* s, ...) {
 	va_list va;
	va_start(va, s);
	printf_va(s, va);
  
  va_end(va);
}

void SystemInterface::printf_va( const char* s, va_list va) 
{
  std::stringstream ss;
	char ch;
	while (*s != 0) 
	{
		ch=*s;
		if (ch!='%') { // normal character: print to out stream
			ss << ch;
		} else { // format character - use special output function
			int float_digits=6; //default: 6 digits for floats
			int padded_length=0;

			s++;  // get format command
			ch=*s;
			// check for digits to get pre-padding
			while ((ch>='0') && (ch<='9')) {
				padded_length = 10*padded_length + ch - '0';
				s++;  
				ch=*s;
			}

			if (ch=='.') { //precision (number of digits after comma for float)
					s++;  // get precision (only one digit allowed here)
					ch=*s;
					float_digits=0;
					while ((ch>='0') && (ch<='9')) {
						float_digits = 10*float_digits + ch - '0';
						s++;  
						ch=*s;
					}

			}
			switch (ch) {
				case 'd': // print integer number
				case 'i': // print integer number
					ss << va_arg(va, int32_t);
				break;
				case 'u': // print integer number
          ss << va_arg(va, int32_t);
				break;
				case 'x': // print integer number as hexadecimal
          ss << va_arg(va, int32_t);
				break;
				case 'b': // print integer number as binary
          ss << va_arg(va, int32_t);
				break;
					
				case 'f': // print float
          ss << va_arg(va, double);
				break;
				case 's': // print string
          ss << va_arg(va, char*);
				break;
				

				case '%': // escape % character (print through)
          ss << ch;
				break;

			}
		}
		s++;
	}
  
  {
    
    std::cout << getTimeBootUs()/1000 << ":  \033[" << color_code_ << "m[A." << (int)self_id << "]  " << ss.str() << "\033[0m  \n";
  }
}



SystemInterface::SystemInterface(uint8_t id) : 
  self_id(id) {
  start_time_ = std::chrono::high_resolution_clock::now();
  color_code_ = "30;33";
  if (self_id == 0) color_code_ = "30;30";
  if (self_id == 1) color_code_ = "30;31";
  if (self_id == 2) color_code_ = "30;32";
  if (self_id == 3) color_code_ = "30;34";
  if (self_id == 4) color_code_ = "30;35";
  if (self_id == 5) color_code_ = "30;36";
  if (self_id == 6) color_code_ = "30;33";
}

double SystemInterface::getTimeBootUs() {
  std::chrono::duration<double> dur_s = std::chrono::high_resolution_clock::now() - start_time_;
  return dur_s.count() * 1000000.0f;
}

void SystemInterface::setSyncTime(std::chrono::time_point<std::chrono::high_resolution_clock> tp) {
  start_time_ = tp;
}



