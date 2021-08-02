/*

    This file is part of the Maude 3 strategy-aware model checker.

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
//	Implementation for class StrategyTransitionGraph.
//

//	utility stuff
#include "macros.hh"
#include "vector.hh"

//	forward declarations
#include "interface.hh"
#include "core.hh"
#include "higher.hh"

//	interface class definitions
#include "symbol.hh"
#include "dagNode.hh"

//	core class definitions
#include "rewriteSearchState.hh"
#include "strategyTransitionGraph.hh"
#include "rule.hh"
#include "rewriteStrategy.hh"

//	mixfix class definitions
#include "token.hh"

//	strategy language definitions
#include "strategyExpression.hh"
#include "decompositionProcess.hh"
#include "modelCheckerSubtermTask.hh"
#include "testStrategy.hh"
#include "trivialStrategy.hh"

//	for the GraphViz generator
#include <sstream>

// the strategy task info is defined in a separate file
#include "strategyTaskInfo.cc"

StrategyTransitionGraph::StrategyTransitionGraph(RewritingContext* initial,
						 StrategyExpression* strategy,
						 const set<int> &opaqueIds,
						 bool biasedMatchrew,
						 bool makeSelfLoops)
  : StrategicSearch(initial, strategy), initial(initial), strategy(strategy),
    opaqueStrategies(opaqueIds), biasedMatchrew(biasedMatchrew), avoidSelfLoops(!makeSelfLoops),  currentSubgraph(0)
{
  // Creates the initial state with the initial decomposition process
  DecompositionProcess* firstProcess =
    new DecompositionProcess(insert(initial->root()),
			     push(EMPTY_STACK, strategy),
			     getDummyExecution(),
			     0);
  State * state = new State(firstProcess);
  state->stateNr = 0;

  SubgraphData *subgraph = new SubgraphData(static_cast<StrategicTask*>(this));

  seen = &subgraph->states;
  subgraphs.append(subgraph);
  (*seen)[0] = state;
  root = this;

  // Adds to the task seen set (optimization for particular but common case)
  getTaskInfo(this).link(TaskInfo::Key(firstProcess->getDagIndex(),
				       firstProcess->getPending()), state);

  // As a task, set the transition graph to itself
  setTransitionGraph(this);
}

StrategyTransitionGraph::~StrategyTransitionGraph()
{
  const size_t nrSubgraphs = subgraphs.size();
  for (size_t i = 0; i < nrSubgraphs; i++)
    if (subgraphs[i] != 0)
      closeSubgraph(i);
}

int
StrategyTransitionGraph::getNextState(int stateNr, int index)
{
  currentSubstate = (*seen)[stateNr];

  //
  // Already calculated
  //
  nrNextStates = currentSubstate->nextStates.length();
  if (index < nrNextStates)
    return currentSubstate->nextStates[index];

  // Before anything else, we import the transitions from the direct dependencies
  nrNextStates += currentSubstate->importDependencies();

  //
  // Otherwise, we need to execute the strategies to get the requested state
  //
  vector<Substate*> dependencyStack;

  while (nrNextStates <= index)
    {
      // These variables are update from outside, from commitState and onCheckpoint
      solutionIndex = NONE;

      // Keeps executing processes until they are exhausted or a successor
      // is found (several of them can be obtainted at once by importing a
      // already seen substate, hence the second condition)
      while (currentSubstate->nextProcess != 0
	     && nrNextStates <= index)
	{
	  StrategicProcess* currentProcess = currentSubstate->nextProcess;

	  Survival s = currentProcess->run(*this);
	  StrategicProcess* t = currentProcess->getNextProcess();

	  // Removes the current process if requested and checks whether
	  // the list is exhausted. In this case, nextProcess will be null.
	  if (s == DIE)
	    {
	      if (currentProcess == t) t = 0;
	      delete currentProcess;
	    }
	  // onCheckpoint may have collapsed currentProcess with a previous
	  // state (although it is unusual with the descend optimization)
	  if (currentProcess == currentSubstate->nextProcess)
	    currentSubstate->nextProcess = t;

	  // Checks if the state provides a solution
	  if (solutionIndex != NONE && !currentSubstate->hasSolution)
	    {
	      currentSubstate->hasSolution = true;
	      makeSelfLoop(currentSubstate, (*seen)[stateNr]->completeDagNode);
	    }

	  //	Now safe to abort.
	  if (RewritingContext::getTraceStatus() && initial->traceAbort())
	    break;
	}

      // Retreats to a continuable state in the dependency stack
      // (in case the current one is not)
      while (currentSubstate->nextProcess == 0
	      && currentSubstate->dependencies.empty()
	      && !dependencyStack.empty())
	{
	  currentSubstate = dependencyStack.back();
	  dependencyStack.pop_back();
	  importFirstDependency(currentSubstate);
	}

      // If there are no more processes to run, but more children are needed
      if (nrNextStates <= index && currentSubstate->nextProcess == 0)
	{
	  if (currentSubstate->dependencies.empty())
	    return NONE;

	  // Tries to find the next state in a dependency
	  Substate* nextDependency = currentSubstate->dependencies.front().dependee;

	  // Prevents loops in the dependencies, looking whether the new dependency
	  // is already in the dependency stack, and rearranging the dependencies in
	  // the positive case
	  const size_t nrDependencies = dependencyStack.size();
	  for (size_t loop = 0; loop < nrDependencies; loop++)
	    if (nextDependency == dependencyStack[loop])
	      {
		solveCyclicDependency(dependencyStack, loop);
		dependencyStack.resize(loop);
		break;
	      }

	  // If no cycle has been removed, the current substate is pushed
	  if (dependencyStack.size() == nrDependencies)
	    dependencyStack.push_back(currentSubstate);

	  currentSubstate = nextDependency;
	  nrNextStates += currentSubstate->importDependencies();
	}

      // The solutions are copied along the dependencies stack to the initial
      // state. However, a solution from a dependency may have already been
      // seen in its dependent, in which case it is not copied. Hence, the
      // number of solutions nrNextState might be overestimated.

      // If we probably have all the requested solutions
      if (nrNextStates > index)
	for (size_t i = dependencyStack.size(); i > 0; )
	  if (importFirstDependency(dependencyStack[--i]))
	    {
	      // Importing an exhausted dependency may destroy its substate,
	      // so it cannot be kept as the currentSubstate
	      currentSubstate = dependencyStack[i];
	      dependencyStack.pop_back();
	    }

      // At this point nrNextStates is accurate, because it has been
      // corrected by importFirstDependency

    }

//  cerr << "\x1b[1m<!> getNextState(" << stateNr << " whose key is " << seen[stateNr]->dagNode << ":"
//    << seen[stateNr]->stackId << ", " << index << ") out of " << seen.length() << " and "
//    << seen[stateNr]->nextStates.length() << " => " << seen[stateNr]->nextStates[index] << "\x1b[0m"<< endl;
  return (*seen)[stateNr]->nextStates[index];
}

int StrategyTransitionGraph::makeSelfLoop(Substate* substate, int dagNode)
{
  if (avoidSelfLoops)
    return 0;

  int nextState;
  //
  // If the substate is a state, is fully explored and does not have any
  // sucessors or dependencies, the self loop can be a real self loop.
  //
  // If the state already has successors or may have them, we cannot make a
  // self-loop because this will allow to stay cycling in the state and then
  // take a transition out of it, which is not an allowed trace.
  //
  // Then we fake a self-looped state with the same DAG node and link the
  // current one to it with a special 'solution' transition.
  //
  TaskInfo &taskInfo = getTaskInfo(root);
  TaskInfo::Key localKey(dagNode, StrategyStackManager::EMPTY_STACK);

  // If we have detected a solution, a substate must have reached the root task
  // with the given dagNode and an empty stack, so seenMap[localKey] exists
  // Assert(taskInfo.seenMap.count(localKey) == 1 && taskInfo.seenMap[localKey] == substate,
  //	 "missing substate");

  // If the state is dead, without any actual or potential successor
  bool dead = substate->nextProcess == 0 && substate->nextStates.empty()
	  && substate->dependencies.empty();

  // The self-loop state must be a dead state, not this
  if (substate->stateNr == NONE || !dead)
    {
      nextState = seen->size();

      // Creates the self loop state
      State* newSelfLoop = new State(dagNode, nextState);
      seen->append(newSelfLoop);

      // Update the link so that it points to the self-loop state
      taskInfo.unlink(localKey);
      taskInfo.link(localKey, newSelfLoop);
    }
  // substate is a dead state, so it can be the self-loop state itself
  else
    nextState = substate->stateNr;

  // Link the current substate with the self-loop state
  substate->nextStates.append(nextState);
  substate->fwdArcs[nextState].insert(Transition());

  // Counts a new succesor and allows garbage collection
  ++nrNextStates;
  MemoryCell::okToCollectGarbage();

  return nextState;
}

StrategyTransitionGraph::State::State(int dagNode, int loopStateNr)
  : dagNode(dagNode),
    stackId(StrategyStackManager::EMPTY_STACK),
    completeDagNode(dagNode) // solutions are always outside
{
  // Specific constructor for self-loop states

  hasSolution = true;		// Unnecessary but looks better in the graph output
  referenceCount = 1;		// Always inserted into the seen list
  stateNr = loopStateNr;

  // Makes the self loop
  nextStates.append(loopStateNr);
  fwdArcs[loopStateNr].insert(Transition());
}

StrategyTransitionGraph::State::State(DecompositionProcess* insertionPoint)
  : dagNode(insertionPoint->getDagIndex()),
    stackId(insertionPoint->getPending()),
    completeDagNode(insertionPoint->getDagIndex())
{
  nextProcess = insertionPoint;
  referenceCount = 1;		// Always inserted into the seen list
  stateNr = 0;			// The initial state always holds the zero index
}

StrategyTransitionGraph::Substate::Substate(int dagNode,
					    StrategyStackManager::StackId pending,
					    StrategicExecution* taskSibling)
  : hasSolution(false),
    nextProcess(new DecompositionProcess(dagNode, pending, taskSibling, 0)),
    referenceCount(0),
    stateNr(NONE)
{
}

StrategyTransitionGraph::State::State(int dagNode,
				      int completeDagNode,
				      StrategyStackManager::StackId pending,
				      StrategicExecution* taskSibling)
  : Substate(dagNode, pending, taskSibling),
    dagNode(dagNode),
    stackId(pending),
    completeDagNode(completeDagNode)
{
  referenceCount = 1;
}

StrategyTransitionGraph::State::State(StrategicProcess* initialProcess,
				      int dagNode,
				      int completeDagNode,
				      StrategyStackManager::StackId pending)
  : dagNode(dagNode),
    stackId(pending),
    completeDagNode(completeDagNode)
{
  nextProcess = initialProcess;
  referenceCount = 1;
}

StrategyTransitionGraph::Substate::~Substate()
{
  const list<Dependency>::const_iterator end = dependencies.end();
  for (list<Dependency>::const_iterator it = dependencies.begin(); it != end; it++)
    it->dependee->free();
}

void StrategyTransitionGraph::commitState(int dagNode,
					  StrategyStackManager::StackId stackId,
					  StrategicExecution* taskSibling,
					  const Transition& transition)
{
  StrategicTask* task = taskSibling->getOwner();
  int nextStateNr;

  // Search this position in the task seen table
  TaskInfo::Key localKey(dagNode, stackId);
  TaskInfo &taskInfo = getTaskInfo(task);
  TaskInfo::SeenMap::const_iterator searchResult = taskInfo.seenMap.find(localKey);

  // This position was already visited, we link to a state or depend on a substate.
  if (searchResult != taskInfo.seenMap.end())
    {
      int stateNr = searchResult->second->stateNr;

      if (stateNr != NONE)
	nextStateNr = stateNr;
      else
	{
	  nrNextStates += currentSubstate->addDependency(searchResult->second);
	  return;
	}
    }
  // Otherwise, we create a new state
  else
    {
      // Computes the full state term when rewriting inside a matchrew
      int completeDag = dagNode;
      {
	ModelCheckerSubtermTask* enclosing = task->getEnclosingSubtermTask();

	while (enclosing != 0)
	  {
	    completeDag = enclosing->onCommitState(completeDag, stackId,
						   taskSibling, transition);

	    // The enclosing task has said that this new state will
	    // be committed in a different way
	    if (completeDag == NONE)
	      return;

	    enclosing = enclosing->getOwner()->getEnclosingSubtermTask();
	  }
      }

      nextStateNr = seen->size();
      seen->append(new State(dagNode, completeDag,
			     stackId, taskSibling));

      // Saves the state number in the state
      (*seen)[nextStateNr]->stateNr = nextStateNr;

      //
      // This is a prescindible optimization that increment the chances of
      // finding an already seen substate or conclude that the new state is
      // a fail. It descend on the task hierarchy, executing only empty
      // decomposition processes and tests.
      //
      if (stackId == StrategyStackManager::EMPTY_STACK && taskSibling->getOwner() != root)
	{
	  // Temporarily replaces the current substate by the new one to descend
	  Substate* originalSubstate = currentSubstate;
	  currentSubstate = (*seen)[nextStateNr];
	  descend();
	  // If the state has been absorbed by an older state
	  if ((*seen)[nextStateNr]->stateNr != nextStateNr)
	    {
	      // The newly created state is not used and the old one
	      // is passed as the new child for the original state
	      seen->contractTo(nextStateNr);
	      nextStateNr = currentSubstate->stateNr;
	      (*seen)[nextStateNr]->referenceCount--;
	      // When states are absorbed nrNextState grows by the
	      // number of successors of the absorbed state, but these
	      // successors do not belong to the current state in this case,
	      // so we have to remove them.
	      nrNextStates -= currentSubstate->nextStates.size();
	    }
	  // If a solution has been reached
	  else if (solutionIndex != NONE && !currentSubstate->hasSolution)
	    {
	      currentSubstate->hasSolution = true;
	      makeSelfLoop(currentSubstate, completeDag);
	      solutionIndex = NONE;
	    }
	  // The process is exhausted without solutions
	  else if (currentSubstate->nextProcess == 0
		   && currentSubstate->dependencies.empty()
		   && currentSubstate->nextStates.empty())
	    {
	      currentSubstate->free();
	      seen->contractTo(nextStateNr);
	      currentSubstate = originalSubstate;
	      return;	// Do not link anything
	    }
	  currentSubstate = originalSubstate;
	}

      // Adds the link if not already done in the descend
      if (taskInfo.seenMap.find(localKey) == taskInfo.seenMap.end())
	taskInfo.link(localKey, (*seen)[nextStateNr]);
    }

  // Links with the parent state
  currentSubstate->nextStates.append(nextStateNr);
  currentSubstate->fwdArcs[nextStateNr].insert(transition);

  // Increment the number of solutions and collects garbage
  ++nrNextStates;
  MemoryCell::okToCollectGarbage();
}

void StrategyTransitionGraph::linkState(int nextState,
					const Transition& transition)
{
  // Links the current state with the nextState
  currentSubstate->nextStates.append(nextState);
  currentSubstate->fwdArcs[nextState].insert(transition);

  // Increment the number of solutions
  ++nrNextStates;
}

int StrategyTransitionGraph::newState(int dagNode,
				      int completeDagNode,
				      StrategyStackManager::StackId stackId,
				      StrategicProcess* initialProcess,
				      const Transition& transition)
{
  StrategicTask* task = initialProcess->getOwner()->getOwner();

  // Computes the full state term when rewriting inside a matchrew
  int completeDag = dagNode;
  {
    ModelCheckerSubtermTask* enclosing = task->getEnclosingSubtermTask();

    while (enclosing != 0)
      {
	completeDag = enclosing->onCommitState(completeDag, stackId,
					       initialProcess, transition);

	enclosing = enclosing->getOwner()->getEnclosingSubtermTask();
      }
  }

  int stateNr = seen->size();
  State* newState = new State(initialProcess, dagNode, completeDagNode, stackId);
  newState->stateNr = stateNr;

  seen->append(newState);
  linkState(stateNr, transition);

  return stateNr;
}

int StrategyTransitionGraph::newSubgraph(int initialDag, StackId pending, StrategicExecution* taskSibling)
{
  // NOTE The identifiers could simply be void* pointers
  int subgraphId = subgraphs.size();

  SubgraphData *subgraph = new SubgraphData(taskSibling->getOwner());
  subgraphs.append(subgraph);
  subgraph->states[0] = new State(new DecompositionProcess(initialDag, pending, taskSibling, 0));

  // Adds to the task seen set (optimization for particular but common case)
  getTaskInfo(taskSibling->getOwner()).link(TaskInfo::Key(initialDag, pending), subgraph->states[0]);

  return subgraphId;
}

bool StrategyTransitionGraph::closeSubgraph(int subgraphId)
{
  SubgraphData *&subgraph = subgraphs[subgraphId];

  size_t nrStates = subgraph->states.length();
  for (size_t i = 0; i < nrStates; i++)
    subgraph->states[i]->free();

  delete subgraph;
  subgraph = 0;

  return true;
}

int StrategyTransitionGraph::selectSubgraph(int subgraphId)
{
  int previousSubgraph = currentSubgraph;

  SubgraphData &subgraph = *subgraphs[subgraphId];

  currentSubgraph = subgraphId;
  seen = &subgraph.states;
  root = subgraph.root;

  // Save nrNextStates
  subgraphs[previousSubgraph]->nrNextStates = nrNextStates;
  nrNextStates = subgraph.nrNextStates;

  // Save currentSubstate
  subgraphs[previousSubgraph]->currentSubstate = currentSubstate;
  currentSubstate = subgraph.currentSubstate;

  return previousSubgraph;
}

bool StrategyTransitionGraph::onCheckpoint(int dagNode,
					   StrategicExecution* taskSibling,
					   int stackId,
					   StrategicProcess* insertionPoint)
{
  StrategicTask* owner = taskSibling->getOwner(); // The task where the point is
  TaskInfo::Key localKey(dagNode, stackId);	  // The local position in the task

  // The pairs (dagNode, stackId) are checked in substate seen table of the
  // owner task information structure. However, in the case of tail calls we
  // directly check them in the base of all tail calls for efficiency and not
  // to loose solutions while descending in the tail call backtrace (the same
  // TaskInfo may appear twice, and the point will already be visited).
  TaskInfo &ownerTaskInfo = getTaskInfo(owner);
  TaskInfo &taskInfo = stackId == EMPTY_STACK && ownerTaskInfo.parent != 0 ?
			 *ownerTaskInfo.parent : ownerTaskInfo;

  // A substate is blank if it does not have any child and it can never have
  // any except those coming from the continuation of (dagNode, stackId)
  bool blank = currentSubstate->nextStates.empty()
	       && currentSubstate->dependencies.empty()
	       // There is a single process which is about to die (the current
	       // process is always about to die when calling onCheckpoint)
	       && currentSubstate->nextProcess->getNextProcess()
		    == currentSubstate->nextProcess;

  // We search for point (dagNode, stackId) in the task seen map
  TaskInfo::SeenMap::const_iterator searchResult = taskInfo.seenMap.find(localKey);

  if (searchResult != taskInfo.seenMap.end())
    {
      // If we arrive to ourselves, we can safely do nothing
      // (because we have already done it)
      if (searchResult->second == currentSubstate)
	return false;	// do not continue

      // (Optional optimization, saves an State object and processing)
      // If both the current substate and the found one are states, the
      // second can absorbe the first as long as the first is blank.
      if (blank && searchResult->second->stateNr != NONE
	  && currentSubstate->stateNr != NONE)
	{
	  int thisStateNr = currentSubstate->stateNr;
	  absorbState(searchResult->second->stateNr, thisStateNr);
	  currentSubstate = (*seen)[thisStateNr];
	  // Count the successors of the absorber state
	  nrNextStates += currentSubstate->nextStates.size();
	}
      // The found substate is added as a dependency to the current one, and
      // its dependencies are imported.
      else
	nrNextStates += currentSubstate->addDependency(searchResult->second);
    }
  // This point (dagNode, stackId) has not been seen yet
  else
    {
      // (Optional optimization, saves a Substate and processing)
      // If the substate is blank, we can link it to the position in
      // the parent because it will not add spurious transitions
      if (blank)
	{
	  // Update the identifying information in case it is a state
	  // (the new pair might be more informative)
	  if (currentSubstate->stateNr != NONE)
	    {
	      State* currentState = static_cast<State*>(currentSubstate);
	      currentState->dagNode = dagNode;
	      currentState->stackId = stackId;
	    }
	  taskInfo.link(localKey, currentSubstate);

	  // When a tail call finishes we simply commit the solution to its base
	  if (stackId == EMPTY_STACK && ownerTaskInfo.parent != 0)
	    {
	      // This will always return SURVIVE since the only case when it may
	      // return DIE (BranchTask with IDLE or FAIL as successAction) is
	      // excluded from model checking
	      taskInfo.rootTask->executionSucceeded(dagNode, insertionPoint);
	      return false;
	    }

	  return true;	// continue with the decomposition process
	}
      // A new substate is created to gather all transition that (directly)
      // follow from the point we are taking
      else
	{
	  // We need a child of taskInfo.rootTask to create the substate
	  // in the tail call return case
	  if (stackId == EMPTY_STACK)
	    while (taskSibling->getOwner() != taskInfo.rootTask)
	      taskSibling = taskSibling->getOwner();
	  Substate* newSubstate = new Substate(dagNode, stackId, taskSibling);
	  taskInfo.link(localKey, newSubstate);
	  currentSubstate->addDependency(newSubstate); // will return 0
	}
    }

  return false;	// do not continue
}

void StrategyTransitionGraph::onStrategyCall(StrategicTask* callTask,
					     int varContext)
{
  TaskInfo* parentTaskInfo = getTaskInfo(callTask->getOwner()).parent;

  TaskInfo::CallMap::const_iterator result = parentTaskInfo->callMap.find(varContext);
  TaskInfo* callTaskInfo;

  // A context which is still alive (although CallTasks for it may not exists)
  if (result != parentTaskInfo->callMap.end())
    {
      // Link the new task with the current TaskInfo
      callTaskInfo = result->second;
      result->second->usersCount++;
    }
  // An unvisited context, we create a new TaskInfo
  else
    {
      callTaskInfo = new TaskInfo(parentTaskInfo, varContext);
      parentTaskInfo->callMap[varContext] = callTaskInfo;
    }

  callTask->setTaskInfo(callTaskInfo);
}

void* StrategyTransitionGraph::getContextGroup(StrategicTask* task)
{
  TaskInfo &taskInfo = getTaskInfo(task);

  // This function is called when a context group is needed,
  // so this task is now the head of a group of contexts
  if (taskInfo.parent == 0)
    {
      // Call task for the same variable context can use the
      // same TaskInfo
      taskInfo.callMap[task->getVarsContext()] = &taskInfo;
      taskInfo.parent = &taskInfo;
    }

  return taskInfo.parent;
}

size_t StrategyTransitionGraph::Substate::addDependency(Substate* dependee)
{
  dependee->referenceCount++;
  State::Dependency dependency;
  dependency.dependee = dependee;
  dependency.alreadyImported = 0;
  dependencies.push_back(dependency);
  return importDependency(--dependencies.end());
}


size_t StrategyTransitionGraph::Substate::importDependency(list<State::Dependency>::iterator iter)
{
  Substate* dependency = iter->dependee;

  size_t nrSuccessors = dependency->nextStates.size();
  size_t newTransitions = 0;

  for (size_t i = iter->alreadyImported; i < nrSuccessors; i++)
    {
      int target = dependency->nextStates[i];
      const set<Transition> &origin = dependency->fwdArcs[target];
      set<Transition> &destination = fwdArcs[target];

      size_t originalSize = fwdArcs[target].size();
      destination.insert(origin.begin(), origin.end());
      // Avoids copying existing transitions
      if (originalSize != destination.size())
	{
	  nextStates.append(target);
	  newTransitions++;
	}
    }
  iter->alreadyImported = nrSuccessors;

  // Import solution mark
  if (!hasSolution && dependency->hasSolution)
    hasSolution = true;

  // If the dependency will not provide solutions anymore, we remove it
  if (dependency->nextProcess == 0 && dependency->dependencies.empty())
    {
      dependency->free();
      dependencies.erase(iter);
    }

  return newTransitions;
}

size_t StrategyTransitionGraph::Substate::importDependencies()
{
  list<State::Dependency>::iterator it = dependencies.begin();
  list<State::Dependency>::iterator end = dependencies.end();

  size_t newTransitions = 0;

  while (it != end)
    newTransitions += importDependency(it++);

  return newTransitions;
}

inline bool
operator<(const StrategyTransitionGraph::Substate::Dependency &left,
	       const StrategyTransitionGraph::Substate::Dependency &right)
{
  return left.dependee < right.dependee || (left.dependee == right.dependee &&
    left.alreadyImported < right.alreadyImported);
}

inline bool
operator==(const StrategyTransitionGraph::Substate::Dependency &left,
		const StrategyTransitionGraph::Substate::Dependency &right)
{
  return left.dependee == right.dependee
	 && left.alreadyImported == right.alreadyImported;
}

void StrategyTransitionGraph::solveCyclicDependency(const vector<Substate*> &dependencyStack, size_t loop)
{
  set<State::Dependency> dependencies;
  size_t stackLength = dependencyStack.size();

  // First round: collect all dependencies outwards the cycle
  for (size_t i = loop; i < stackLength; i++)
    {
      const list<State::Dependency>::const_iterator end = dependencyStack[i]->dependencies.end();
      for (list<State::Dependency>::const_iterator it = ++dependencyStack[i]->dependencies.begin(); it != end; it++)
	dependencies.insert(*it);
    }

  // Second round: add them to all substates in the cycle
  for (size_t i = loop; i < stackLength; i++)
    {
      list<State::Dependency> &currentDependencies = dependencyStack[i]->dependencies;

      for (auto& dep : dependencies)
	{
	  if (std::find(currentDependencies.begin(), currentDependencies.end(), dep)
	       == currentDependencies.end())
	    {
	      dep.dependee->referenceCount++;
	      currentDependencies.push_back(dep);
	      // Dependencies are not imported yet
	    }
	}
    }

  // Third round: the cycle of dependencies is removed
  for (size_t i = loop; i < stackLength; i++)
    {
      dependencyStack[i]->dependencies.pop_front();
      dependencyStack[i]->free();
    }
}

void
StrategyTransitionGraph::absorbState(int absorber, int absorbed)
{
  State* absorbedState = (*seen)[absorbed];
  // Assert(absorbedState->nextStates.length() == 0, "absorbed non-empty state");

  // The nextProcess of the absorbed state is what we are executing now,
  // and it will be immediately deleted in getNextState. However, the pointer
  // will not be set to null there, since currentState will have been replaced
  // by the absorber state, so it must be done here.
  absorbedState->nextProcess = nullptr;

  if (absorbedState->referenceCount-- == 1)
    delete absorbedState;
  else
      absorbedState->addDependency((*seen)[absorber]);

  (*seen)[absorbed] = (*seen)[absorber];
  (*seen)[absorbed]->referenceCount++;
}

inline bool
StrategyTransitionGraph::descendProcess(StrategicProcess* proc) const
{
  if (DecompositionProcess* dp = dynamic_cast<DecompositionProcess*>(proc))
    {
      return dynamic_cast<TestStrategy*>(top(dp->getPending())) != 0 ||
	dynamic_cast<TrivialStrategy*>(top(dp->getPending())) != 0 ||
	(dp->getPending() == StrategyStackManager::EMPTY_STACK
	  && dp->getOwner() != root);
    }

  return false;
}

void StrategyTransitionGraph::descend()
{
  //
  // Execute all available deterministic processes. It is used after a
  // new rewrite or atomic operation. The concept of "deterministic
  // process" is defined by descendProcess.
  //

  // Saves the initial currentSubstate, as it may change
  Substate* initialSubstate = currentSubstate;

  // We will be using the process, so we add a reference
  currentSubstate->referenceCount++;

  while (initialSubstate == currentSubstate
	  && currentSubstate->nextProcess != 0
	  && solutionIndex == NONE
	  && descendProcess(currentSubstate->nextProcess))
    {
      StrategicProcess* currentProcess = currentSubstate->nextProcess;
      Survival s = currentProcess->run(*this);
      StrategicProcess* t = currentProcess->getNextProcess();


      if (s == DIE)
	{
	  if (currentProcess == t) t = 0;
	  delete currentProcess;
	}

      initialSubstate->nextProcess = t;
    }

  // We can get rid of the initialSubstate
  initialSubstate->free();
}

bool StrategyTransitionGraph::importFirstDependency(Substate* dependent)
{
  Assert(dependent->dependencies.size() > 0, "no first dependency exists");

  list<State::Dependency>::iterator dependency = dependent->dependencies.begin();

  size_t expectedImports = dependency->dependee->nextStates.size()
			    - dependency->alreadyImported;
  bool finished = dependency->dependee->nextProcess == 0
		  && dependency->dependee->dependencies.empty();

  size_t actualImports = dependent->importDependency(dependency);

  if (actualImports != expectedImports)
    nrNextStates -= (expectedImports - actualImports);

  return finished;
}


TaskInfo&
StrategyTransitionGraph::getTaskInfo(StrategicTask* task)
{
  TaskInfo* taskInfo = task->getTaskInfo();

  if (taskInfo == 0)
    {
      taskInfo = new TaskInfo(task);
      task->setTaskInfo(taskInfo);
    }

  return *taskInfo;
}

void deleteTaskInfo(TaskInfo* taskInfo)
{
  taskInfo->free();
}

inline void writeInt(ostream& out, const int& value)
{
  out.write(reinterpret_cast<const char*>(&value), sizeof(int));
}

struct DumpTable
{
  enum ResourceType
  {
    DAG = 0,
    STRAT = 1,
    TOKEN = 2
  };

  struct Resource
  {
    Resource(ResourceType type, int value)
      : type(type), value(value) {}

    ResourceType type;
    int value;
  };

  int position(ResourceType type, int value)
  {
    map<int,int>::iterator it = tables[type].find(value);

    if (it != tables[type].end())
      return it->second;

    tables[type][value] = count;
    table.push_back(Resource(type, value));

    return count++;
  }

  vector<Resource> table;
  int count;
  map<int,int> tables[3];

  DumpTable() : count(0) {}
};

DagNode*
StrategyTransitionGraph::findNextSolution()
{
  CantHappen("findNextSolution called on StrategyTransitionGraph");
  return nullptr;
}

void StrategyTransitionGraph::dotDump(ostream& s) const
{
  size_t nrStates = seen->length();

  DumpTable table;

  ostream::pos_type stateTable = s.tellp();
  int firstPosition = stateTable + ostream::off_type((nrStates + 1) * sizeof(int));
  writeInt(s, firstPosition);
  s.seekp(sizeof(int) * nrStates, std::ios_base::cur);

  // State with a different index may have collapsed,
  // we do not copy them twice
  map<State*, int> seenStates;

  for (size_t i = 0; i < nrStates; i++)
    {
      State* current = (*seen)[i];

      // Collapsed states are detected here
      {
	map<State*, int>::const_iterator seen = seenStates.find(current);
	ostream::pos_type currentPosition = s.tellp();

	if (seen != seenStates.end())
	  {
	    // The just-written pointer for the i-th state is corrected
	    // to point to the already dumped state
	    s.seekp(stateTable + ostream::off_type(i * sizeof(int)));
	    writeInt(s, seen->second);
	    // The pointer for the (i+1)-th state must also be written
	    writeInt(s, currentPosition);
	    s.seekp(currentPosition);
	    continue;
	  }
	else
	  seenStates[current] = currentPosition;
      }

      int position = table.position(DumpTable::DAG, current->completeDagNode);
      writeInt(s, position);
      position = table.position(DumpTable::STRAT, current->stackId);
      writeInt(s, position);
      s.put(current->hasSolution ? 1 : 0);

      int nrSuccesors = current->nextStates.size();
      writeInt(s, nrSuccesors);
      for (int j = 0; j < nrSuccesors; j++)
	{
	  writeInt(s, current->nextStates[j]);

	  const Transition & transition = *current->fwdArcs[current->nextStates[j]].begin();
	  switch (transition.getType())
	    {
	      case SOLUTION : s.put(0); break;
	      case RULE_APPLICATION :
		s.put(1);
		position = table.position(DumpTable::TOKEN, transition.getRule()->getLabel().id());
		writeInt(s, position);
		break;
	      case OPAQUE_STRATEGY :
		s.put(2);
		position = table.position(DumpTable::TOKEN, transition.getStrategy()->id());
		writeInt(s, position);
		break;
	    }
	}

      // Puts the next pointer
      ostream::pos_type currentPosition = s.tellp();
      s.seekp(stateTable + ostream::off_type((i+1) * sizeof(int)));
      writeInt(s, currentPosition);
      s.seekp(currentPosition);
    }
  // Print the sources table
  ostream::pos_type resourceTable = s.tellp();
  int nrResources = table.count;
  writeInt(s, nrResources);
  resourceTable += sizeof(int);
  s.seekp(sizeof(int) * (nrResources + 1), std::ios_base::cur);

  for (int i = 0; i < nrResources; i++)
  {
    ostream::pos_type currentPosition = s.tellp();
    s.seekp(resourceTable);
    resourceTable += sizeof(int);
    writeInt(s, currentPosition);
    s.seekp(currentPosition);

    DumpTable::Resource &resource = table.table[i];
    switch (resource.type)
    {
      case DumpTable::DAG :  s << getCanonical(resource.value); break;
      case DumpTable::STRAT : s << top(resource.value); break;
      case DumpTable::TOKEN :  s << Token::name(resource.value); break;
    }
  }

  ostream::pos_type currentPosition = s.tellp();
  s.seekp(resourceTable);
  writeInt(s, currentPosition);
}

int StrategyTransitionGraph::getNrRealStates() const
{
  int nrSeen = seen->size();
  int nrStates = 0;

  for (int i = 0; i < nrSeen; i++)
    if ((*seen)[i]->stateNr == i)
      nrStates++;

  return nrStates;
}
