/*

    This file is part of the Maude 3 interpreter.

    Copyright 2021-2024 SRI International, Menlo Park, CA 94025, USA.

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
//	Main directory manipulation code.
//

void
DirectoryManagerSymbol::getOpenDirectory(DagNode* directoryArg, int& fd, OpenDirectory*& ofp)
{
  //
  //	In order for message to have been delivered there must have been
  //	an open directory object so any failure to find it is an internal error.
  //
  Assert(directoryArg->symbol() == directoryOidSymbol, "bad directory symbol " << directoryArg);
  DagNode* idArg = safeCast(FreeDagNode*, directoryArg)->getArgument(0);
  DebugSave(ok, succSymbol->getSignedInt(idArg, fd));
  Assert(ok, "bad directory number " << directoryArg);
  DirectoryMap::iterator i = openDirectories.find(fd);
  Assert(i != openDirectories.end(), "didn't find open directory " << directoryArg);
  ofp = &(i->second);
}

void
DirectoryManagerSymbol::openDirectory(FreeDagNode* message, ObjectSystemRewritingContext& context)
{
  Assert(message->getArgument(0)->symbol() == this, "misdirected message");
  if (allowDir)
    {
      errorReply("Not supported in this Windows port.", message, context);
    }
  else
    {
      IssueAdvisory("operations on directories disabled.");
      errorReply("Directory operations disabled.", message, context);
    }
}

void
DirectoryManagerSymbol::makeDirectory(FreeDagNode* message, ObjectSystemRewritingContext& context)
{
  Assert(message->getArgument(0)->symbol() == this, "misdirected message");
  if (allowDir)
    {
      DagNode* pathArg = message->getArgument(2);
      if (pathArg->symbol() == stringSymbol)
	{
	  const Rope& path = safeCast(StringDagNode*, pathArg)->getValue();
	  char* pathStr = path.makeZeroTerminatedString();
	  int result = mkdir(pathStr);
	  delete [] pathStr;
	  if (result == 0)
	    trivialReply(madeDirectoryMsg, message, context);
	  else
	    errorReply(strerror(errno), message, context);
	}
      else
	errorReply("Bad directory name.", message, context);
    }
  else
    {
      IssueAdvisory("operations on directories disabled.");
      errorReply("Directory operations disabled.", message, context);
    }
}

void
DirectoryManagerSymbol::removeDirectory(FreeDagNode* message, ObjectSystemRewritingContext& context)
{
  Assert(message->getArgument(0)->symbol() == this, "misdirected message");
  if (allowDir)
    {
      DagNode* pathArg = message->getArgument(2);
      if (pathArg->symbol() == stringSymbol)
	{
	  const Rope& path = safeCast(StringDagNode*, pathArg)->getValue();
	  char* pathStr = path.makeZeroTerminatedString();
	  int result = rmdir(pathStr);
	  delete [] pathStr;
	  if (result == 0)
	    trivialReply(removedDirectoryMsg, message, context);
	  else
	    errorReply(strerror(errno), message, context);
	}
      else
	errorReply("Bad directory name.", message, context);
    }
  else
    {
      IssueAdvisory("operations on directories disabled.");
      errorReply("Directory operations disabled.", message, context);
    }
}

void
DirectoryManagerSymbol::handleSymbolicLink(Rope path,
					   Rope name,
					   FreeDagNode* message,
					   ObjectSystemRewritingContext& context)
{
  errorReply("Not supported in this Windows port.", message, context);
}

void
DirectoryManagerSymbol::getDirectoryEntry(FreeDagNode* message, ObjectSystemRewritingContext& context)
{
 errorReply("Not supported in this Windows port.", message, context);
}

void
DirectoryManagerSymbol::closeDirectory(FreeDagNode* message, ObjectSystemRewritingContext& context)
{
  DagNode* directoryName = message->getArgument(0);
  int fd;
  OpenDirectory* odp;
  getOpenDirectory(directoryName, fd, odp);
  closedir(odp->dir);
  openDirectories.erase(fd);
  context.deleteExternalObject(directoryName);
  trivialReply(closedDirectoryMsg, message, context);
}

void
DirectoryManagerSymbol::cleanUp(DagNode* objectId)
{
  int fd;
  OpenDirectory* odp;
  getOpenDirectory(objectId, fd, odp);
  DebugAdvisory("cleaning up " << objectId);
  closedir(odp->dir);
  openDirectories.erase(fd);
}
