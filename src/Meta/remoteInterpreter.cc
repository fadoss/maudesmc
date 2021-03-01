/*

    This file is part of the Maude 3 interpreter.

    Copyright 2020-2023 SRI International, Menlo Park, CA 94025, USA.

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
//      Remote metaInterpreters: parent side
//

// FIXME: This needs to live somewhere else, and perhaps be used by ProcessManagerSymbol
#include <sys/types.h>
//#include <sys/socket.h>
#include <fcntl.h>

int
makeNonblockingSocketPair(int pair[2], bool readOnly)
{
}

DagNode*
InterpreterManagerSymbol::makeErrorReply(const Rope& errorMessage,
					 FreeDagNode* originalMessage)
{
  Vector<DagNode*> reply(3);
  reply[0] = originalMessage->getArgument(1);
  reply[1] = originalMessage->getArgument(0);
  reply[2] = new StringDagNode(stringSymbol, errorMessage);
  return interpreterErrorMsg->makeDagNode(reply);
}

bool
InterpreterManagerSymbol::createRemoteInterpreter(FreeDagNode* originalMessage,
						  ObjectSystemRewritingContext& context,
						  int id)
{
  //
  // Remote interpreters are not supported on Windows.
  //
  errorReply("Remote interpreters are not supported in this Windows port.", originalMessage, context);
  return true;
}

bool
InterpreterManagerSymbol::remoteHandleMessage(FreeDagNode* message,
					      ObjectSystemRewritingContext& context,
					      RemoteInterpreter* r)
{
  //
  // Remote interpreters are not supported on Windows.
  //
  return true;
}

void
InterpreterManagerSymbol::remoteHandleReply(RemoteInterpreter* r, const Rope& reply)
{
  //
  // Remote interpreters are not supported on Windows.
  //
}

void
InterpreterManagerSymbol::doChildExit(pid_t childPid)
{
  //
  // Remote interpreters are not supported on Windows.
  //
}
