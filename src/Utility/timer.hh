/*

    This file is part of the Maude 3 interpreter.

    Copyright 1997-2003 SRI International, Menlo Park, CA 94025, USA.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.

*/

//
//      Class for timers.
//
#ifndef _timer_hh_
#define _timer_hh_
#include "timeStuff.hh"

class Timer
{
public:
  Timer(bool startRunning = false);
  
  bool getTimes(Int64& real, Int64& virt, Int64& prof) const;
  bool isRunning() const;
  void start();
  void stop();

private:
  static Int64 calculateMicroseconds(Int64 startTime,
				     Int64 stopTime);

  static void getTimers(Int64& real, Int64& virt, Int64& prof);

  Int64 realAcc;
  Int64 virtAcc;
  Int64 profAcc;
  Int64 realStartTime;
  Int64 virtStartTime;
  Int64 profStartTime;
  bool running;
  bool valid;
};

inline bool
Timer::isRunning() const
{
  return running;
}

#endif
