/*

    This file is part of the Maude 3 interpreter.

    Copyright 1997-2006 SRI International, Menlo Park, CA 94025, USA.

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
//      Implementation for CallTask.
//

//	utility stuff
#include "macros.hh"
#include "vector.hh"

//      forward declarations
#include "interface.hh"
#include "core.hh"
#include "strategyLanguage.hh"

//	core class definitions
#include "symbol.hh"
#include "rewriteStrategy.hh"

//	higher class definitions
#include "strategyTransitionGraph.hh"

//	strategy language class definitions
#include "callTask.hh"
#include "decompositionProcess.hh"
#include "strategicSearch.hh"

CallTask::CallTask(StrategicSearch& searchObject,
		   int startIndex,
		   RewriteStrategy* callee,
		   StrategyExpression* strategy,
		   StrategyStackManager::StackId pending,
		   VariableBindingsManager::ContextId varBinds,
		   StrategicExecution* sibling,
		   StrategicProcess* insertionPoint)
  : StrategicTask(sibling, varBinds),
    searchObject(searchObject),
    callee(callee)
{
  StrategyTransitionGraph* transitionGraph = getTransitionGraph();

  // The pending state within the call
  int nextPending = searchObject.push(StrategyStackManager::EMPTY_STACK, strategy);

  // When model checking and doing a tail call, whether the current execution
  // point has been visited is checked with onCheckpoint after onStrategyCall.
  // If visited, this task will be inmediately destroyed outside, but since
  // onStrategyCall may create a substate with a decomposition process on its
  // own, we must call it once the task has been created.
  bool shouldContinue = transitionGraph == 0 || pending != StrategyStackManager::EMPTY_STACK;

  if (!shouldContinue)
    {
      transitionGraph->onStrategyCall(this, varBinds);
      shouldContinue = transitionGraph->onCheckpoint(startIndex, getDummyExecution(), nextPending, insertionPoint);
    }

  // When we are not model checking, this is not a tail call, or onCheckPoint
  // allow us to continue, we create a decomposition process for the call
  if (shouldContinue)
    (void) new DecompositionProcess(startIndex,
				    nextPending,
				    getDummyExecution(),
				    insertionPoint);

  StrategicTask::pending = pending;

  //
  // When running an opaque strategy, model checking is disabled inside
  //
  if (transitionGraph != 0 && callee != nullptr && transitionGraph->isOpaque(callee->id()))
    setTransitionGraph(0);
  // Otherwise, callee is set to nullptr because the information is not needed and we
  // can then use callee != nullptr to detect if we are running an opaque strategy
  else
    this->callee = nullptr;
}

StrategicExecution::Survival
CallTask::executionSucceeded(int resultIndex, StrategicProcess* insertionPoint)
{
  StrategyTransitionGraph* transitionGraph = getOwner()->getTransitionGraph();

  // For opaque strategies, we inform that a new state has been reached
  if (callee != nullptr)
    transitionGraph->commitState(resultIndex, pending, this, callee);
  // Otherwise, we recover the execution just after the strategy call
  else
   resumeOwner(resultIndex, pending, insertionPoint);

  return SURVIVE;
}

StrategicExecution::Survival
CallTask::executionsExhausted(StrategicProcess*)
{
  searchObject.closeContext(getVarsContext());
  return DIE;
}
