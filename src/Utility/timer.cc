/*

    This file is part of the Maude 3 interpreter.

    Copyright 1997-2023 SRI International, Menlo Park, CA 94025, USA.

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
//      Implementation for class Timer.
//
#include <signal.h>

// Windows API
#include <processthreadsapi.h>
#include <profileapi.h>

//      utility stuff
#include "macros.hh"

#include "timer.hh"

local_inline Int64
Timer::calculateMicroseconds(Int64 startTime,
			     Int64 stopTime)
{
  return (stopTime - startTime) / 10;
}

local_inline
void Timer::getTimers(Int64& real, Int64& virt, Int64& prof)
{
  // Get the current process kernel and user time (in time units of 100ns)
  HANDLE currentProcess = GetCurrentProcess();
  FILETIME creation, exit, kernel, user;
  ULARGE_INTEGER large;
  GetProcessTimes(currentProcess, &creation, &exit, &kernel, &user);

  large.u.LowPart = kernel.dwLowDateTime;
  large.u.HighPart = kernel.dwHighDateTime;

  // Virtual time is user time
  virt = large.QuadPart;
  // Profiler time is the sum of user and kernel times
  prof = large.QuadPart;

  large.u.LowPart = user.dwLowDateTime;
  large.u.HighPart = user.dwHighDateTime;

  prof += large.QuadPart;

  // Get real time from the interrupt time (100ms)
  LARGE_INTEGER counter, frequency;
  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);

  real = (10000000 * counter.QuadPart) / frequency.QuadPart;
}

Timer::Timer(bool startRunning)
{
  realAcc = 0;
  virtAcc = 0;
  profAcc = 0;
  running = false;
  valid = true;
  if (startRunning)
    {
      running = true;
      getTimers(realStartTime, virtStartTime, profStartTime);
    }
}

void
Timer::start()
{
  if (!running && valid)
    {
      //
      //	We get new start times.
      //
      running = true;
      getTimers(realStartTime, virtStartTime, profStartTime);
    }
  else
    valid = false;
}

void 
Timer::stop()
{
  if (running && valid)
    {
      //
      //	We accumulate the microseconds since last start().
      //
      Int64 realStopTime;
      Int64 virtStopTime;
      Int64 profStopTime;
      getTimers(realStopTime, virtStopTime, profStopTime);
      running = false;
      realAcc += calculateMicroseconds(realStartTime, realStopTime);
      virtAcc += calculateMicroseconds(virtStartTime, virtStopTime);
      profAcc += calculateMicroseconds(profStartTime, profStopTime);
    }
  else
    valid = false;
}

bool
Timer::getTimes(Int64& real, Int64& virt, Int64& prof) const
{
  if (valid)
    {
      //
      //	We return the accumulated times plus any time since last start() if we are running.
      //
      real = realAcc;
      virt = virtAcc;
      prof = profAcc;
      if (running)
	{
	  Int64 realStopTime;
	  Int64 virtStopTime;
	  Int64 profStopTime;
         getTimers(realStopTime, virtStopTime, profStopTime);
	  real += calculateMicroseconds(realStartTime, realStopTime);
	  virt += calculateMicroseconds(virtStartTime, virtStopTime);
	  prof += calculateMicroseconds(profStartTime, profStopTime);
	}
      return true;
    }
  return false;
}
