/*

    This file is part of the Maude 3 interpreter.

    Copyright 2020 SRI International, Menlo Park, CA 94025, USA.

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

void
ProcessManagerSymbol::errorReply(const Rope& errorMessage,
				 FreeDagNode* originalMessage,
				 ObjectSystemRewritingContext& context)
{
  Vector<DagNode*> reply(3);
  reply[1] = originalMessage->getArgument(0);
  reply[2] = new StringDagNode(stringSymbol, errorMessage);
  DagNode* target = originalMessage->getArgument(1);
  reply[0] = target;
  context.bufferMessage(target, processErrorMsg->makeDagNode(reply));
}
