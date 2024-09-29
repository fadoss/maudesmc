/*

    This file is part of the Maude 3 interpreter.

    Copyright 1997-2021 SRI International, Menlo Park, CA 94025, USA.

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
//      Implementation for class IO_Manager.
//

#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <windows.h>

//      utility stuff
#include "macros.hh"
#include "rope.hh"

//	line editing stuff
#define _LB_LIBRARY 1
#define _LB_EXECUTABLE 2
#if LIBRARY_BUILD == _LB_LIBRARY
// readline is disabled when building Maude as a library
#undef USE_READLINE
#endif

#ifdef USE_READLINE
#if HAVE_SYS_TERMIOS_H
#include <sys/termios.h>
#endif
#include <readline/readline.h>
#include <readline/history.h>
#endif

//	IO Stuff class definitions
#include "autoWrapBuffer.hh"
#include "IO_Manager.hh"

pid_t IO_Manager::stdinOwner = 0;

#if LIBRARY_BUILD != _LB_EXECUTABLE
IO_Manager::IO_Manager()
{
  usingReadline = false;
  rdline = 0;	// full line (must be stored to free it)
  line = 0;	// remaining line fragment (because of maxSize)
  usePromptsAnyway = false;
  contFlag = false;
  wrapOut = 0;
  wrapErr = 0;
  savedOut = 0;
  savedErr = 0;
  firstUnused = 0;
  bufferEnd = 0;
  bufferSize = 0;
  buffer = 0;

  DWORD mode;
  HANDLE handle;

  handle = GetStdHandle(STD_OUTPUT_HANDLE);

  if (GetConsoleMode(handle, &mode))
    {
      mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
      SetConsoleMode(handle, mode);
    }

  handle = GetStdHandle(STD_ERROR_HANDLE);

  if (GetConsoleMode(handle, &mode))
    {
      mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
      SetConsoleMode(handle, mode);
    }
}
#endif

IO_Manager::~IO_Manager()
{
#ifdef USE_READLINE
  if (usingReadline)
    write_history(".maude_history");

  if (rdline != 0)
  {
    free(rdline);
    rdline = 0;
  }
#endif
}

void
IO_Manager::setCommandLineEditing(size_t /* lineLength */, size_t historyLength)
{
#ifdef USE_READLINE
  usingReadline = true;

  using_history();
  read_history(".maude_history");
  stifle_history(historyLength);

  // This makes readline return nullptr when Ctrl+C is entered
  rl_getc_function = getc;
#endif
}

#if LIBRARY_BUILD != _LB_EXECUTABLE
void
IO_Manager::setAutoWrap(bool lineWrapping)
{
  Assert(wrapOut == 0 && wrapErr == 0 && savedOut == 0 && savedErr == 0, "already set");
  //
  //	Set up autowrapping of standard output and standard error.
  //
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;

  {
    int columns = DEFAULT_COLUMNS;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &consoleInfo) != 0)
      columns = consoleInfo.srWindow.Right - consoleInfo.srWindow.Left + 1;
    wrapOut = new AutoWrapBuffer(cout.rdbuf(), columns, true, &waitUntilSafeToAccessStdin);
    savedOut = cout.rdbuf(wrapOut);
  }
  {
    int columns = DEFAULT_COLUMNS;
    // cout << "err columns " << columns << '\n';
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &consoleInfo) != 0)
      columns = consoleInfo.srWindow.Right - consoleInfo.srWindow.Left + 1;
    wrapErr = new AutoWrapBuffer(cerr.rdbuf(), columns, false, &waitUntilSafeToAccessStdin);
    savedErr = cerr.rdbuf(wrapErr);
  }
}

void
IO_Manager::unsetAutoWrap()
{
  //
  //	Undo autowrapping of standard output and standard error if we
  //	originally wrapped them.
  //
  if (wrapOut != 0 && savedOut != 0)
    {
      (void) cout.rdbuf(savedOut);
      delete wrapOut;
      wrapOut = 0;
      savedOut = 0;
    }
  if (wrapErr != 0 && savedErr != 0)
    {
      (void) cerr.rdbuf(savedErr);
      delete wrapErr;
      wrapErr = 0;
      savedErr = 0;
    }
}
#endif

void
IO_Manager::waitUntilSafeToAccessStdin()
{
  //
  //	If a child process is reading from stdin we need to wait
  //	until it exits to avoid a race condition where tecla saves
  //	the terminal in a RAW state induced by the child.
  //
  if (stdinOwner)
    {
      if (stdinOwner == getpid())
	{
	  //
	  //	We're the child that is reading from stdin.
	  //
	  return;
	}
      //
      //	The child should exit once it is done with stdin
      //	so we wait for it, but leave it in a waitable state.
      //
      HANDLE pHandle = OpenProcess(SYNCHRONIZE, false, stdinOwner);
      WaitForSingleObject(pHandle, INFINITE); 
      CloseHandle(pHandle);
      stdinOwner = 0;
    }
}

ssize_t
IO_Manager::getInput(char* buf, size_t maxSize, FILE* stream)
{
  if (stream != stdin)
    {
      //
      //	Some stdio libraries (notably that of linux) have a nasty habit
      //	of restarting slow system calls aborted by signals. We avoid
      //	this behaviour by doing input directly from the OS.
      //
      return read(fileno(stream), buf, maxSize);
    }
  //
  //	In case we have a child process that is accessing stding.
  //
  waitUntilSafeToAccessStdin();

#ifdef USE_READLINE
  if (usingReadline)
    {
      if (line == 0)
	{
	  rdline = readline(contFlag ? contPrompt.c_str() : prompt.c_str());

	  // Update the word wrapping buffers size just in case
	  // the terminal width has changed
	  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
	  int columns;
	  if (wrapOut != 0 && GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &consoleInfo) != 0
	      && (columns = consoleInfo.srWindow.Right - consoleInfo.srWindow.Left + 1) > 0)
	    wrapOut->setLineWidth(columns);
	  if (wrapErr != 0 && GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &consoleInfo) != 0
	      && (columns = consoleInfo.srWindow.Right - consoleInfo.srWindow.Left + 1) > 0)
	    wrapErr->setLineWidth(columns);
	  contFlag = true;

	  if (rdline == 0)
	    return 0;
	  else if (*rdline)
	    add_history(rdline);

	  // Appends a newline character (readline removes it)
	  int lineLen = strlen(rdline);
	  rdline = (char *) realloc(rdline, lineLen + 2);
	  rdline[lineLen] = '\n';
	  rdline[lineLen+1] = '\0';

	  line = rdline;
	}

      size_t n;
      for (n = 0;; n++)
	{
	  char c = *line;
	  if (c == '\0')
	    {
	      free(rdline);
	      rdline = 0;
	      line = 0;
	      break;
	    }
	  if (n == maxSize)
	    break;
	  *buf++ = c;
	  ++line;
	}
      return n;
    }
#endif
  //
  //	Read from stdin without using tecla.
  //	It's important that we only read a line at a time from stdin,
  //	now that we allow Maude programs to read from stdin. Otherwise
  //	characters past \n could be buffered by the lexer.
  //
  if (usePromptsAnyway)
    {
      //
      //	We don't generate continuation prompts in this case to avoid breaking IOP.
      //
      if (!contFlag)
	{
	  cout << prompt.c_str() << flush;
	  contFlag = true;
	}
    }
  return readFromStdin(buf, maxSize);
}

#if LIBRARY_BUILD != _LB_EXECUTABLE
ssize_t
IO_Manager::readFromStdin(char* buf, size_t maxSize)
{
  //
  //	We bypass stdlib because we want to get whatever is available
  //	from a pipe without waiting for a \n to avoid breaking IOP.
  //
  //	But we can't use read() in place of this function because
  //	we don't want lexer swallowing characters past \n since they
  //	may be wanted by the object system standard stream functionality.
  //
  //	Instead we keep a local buffer. We return whatever is in the
  //	local buffer up to \n, maxSize, or the end of the buffer.
  //	If the local buffer is empty, we refill it with whatever
  //	is available from read(), and we don't make another call to
  //	read() (which could block on a pipe or socket) until the local
  //	buffer is empty.
  //
  if (firstUnused >= bufferEnd)
    {
      //
      //	No buffered characters, need to do a read() and maybe block.
      //
      if (bufferSize < maxSize)
	{
	  //
	  //	Need to reallocate our buffer first.
	  //
	  delete [] buffer;
	  buffer = new char[maxSize];
	  bufferSize = maxSize;
	}

      firstUnused = 0;
      bufferEnd = read(STDIN_FILENO, buffer, maxSize);
      if (bufferEnd <= 0)
	{
	  if (isatty(STDIN_FILENO))
	    cout << '\n' << flush;
	  return bufferEnd;  // EOF or error
	}
    }
  //
  //	Return the buffered characters, up to \n, maxSize or end of local buffer.
  //
  size_t  i = 0;
  for (;;)
    {
      char c = buffer[firstUnused];
      buf[i] = c;
      ++firstUnused;
      ++i;
      if (c == '\n' || i == maxSize || firstUnused == bufferEnd)
	break;
    }
  return i;
}
#endif

Rope
IO_Manager::getLineFromStdin(const Rope& prompt)
{
  //
  //	In case we have a child process that is accessing stdin.
  //
  waitUntilSafeToAccessStdin();
  //
  //	Get a line as a Rope, possibly using Tecla.
  //
#ifdef USE_READLINE
  if (usingReadline && isatty(STDIN_FILENO))
    {
      char* promptString = prompt.makeZeroTerminatedString();
      //  ignore any partial line left in line
      if (rdline != 0)
	{
	  free(rdline);
	  rdline = 0;
	  line = 0;
	}
      rdline = readline(promptString);
      delete [] promptString;
      if (rdline == 0)
	return Rope();  // return empty rope on error or eof
      else if (*rdline)
	add_history(rdline);
      Rope result(rdline);
      free(rdline);
      rdline = 0;
      return result;
    }
#endif
  //
  //	Non-Tecla case. Either Tecla not compiled it, or disabled or
  //	we are getting the line from a file or pipe.
  //	We keep reading, respecting buffered characters, until we get to \n or EOF.
  //
  char* promptString = prompt.makeZeroTerminatedString();
  cout << promptString << flush;
  delete [] promptString;
  //
  //	We keep reading and accumulating characters until we hit \n, EOF or error.
  //
  Rope result;
  for (;;)
    {
      char buf[BUFFER_SIZE];
      ssize_t nrRead = readFromStdin(buf, BUFFER_SIZE);
      if (nrRead <= 0)
	break;
      Rope t(buf, nrRead);
      result += t;
      if (buf[nrRead - 1] == '\n')
	break;
    }
  return result;
}
