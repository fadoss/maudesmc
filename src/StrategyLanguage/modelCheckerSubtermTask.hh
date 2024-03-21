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
//	The base class for the matchrew subterm rewriting implementation
//	for the model checker.
//
#ifndef _modelCheckerSubtermTask_hh_
#define _modelCheckerSubtermTask_hh_

//	core class definitions
#include "dagNode.hh"

//	higher class definitions
#include "pattern.hh"
#include "matchSearchState.hh"
#include "strategyTransitionGraph.hh"

//	strategy language definitions
#include "strategyLanguage.hh"
#include "strategicTask.hh"
#include "variableBindingsManager.hh"

#include <memory>

class ModelCheckerSubtermTask : public StrategicTask
{
  NO_COPYING(ModelCheckerSubtermTask);

public:
  /**
   * @brief Creates a task for the matchrew strategy execution.
   *
   * @param searchObject The global search object.
   * @param strategy Matchrew strategy in execution.
   * @param subterms Subterms (original or already rewritten)
   * @param searchState Main pattern match state.
   * @param extensionInfo Extension information, whenever the match is with extension.
   * @param searchPosition This match position in the (reusable) searchState.
   * @param pending Stack of pending strategies.
   * @param sibling Task's sibling.
   * @param varBinds Variable bindings.
   * @param insertionPoint Insertion point in the list of processes.
   */
  ModelCheckerSubtermTask(
    StrategicSearch &searchObject,
    SubtermStrategy * strategy,
    shared_ptr<MatchSearchState> searchState,
    ExtensionInfo * extensionInfo,
    MatchSearchState::PositionIndex searchPosition,
    StrategyStackManager::StackId pending,
    VariableBindingsManager::ContextId varBinds,
    StrategicExecution * sibling
  );

  /**
   * @brief Copy constructor.
   */
  ModelCheckerSubtermTask(ModelCheckerSubtermTask &task,
			  const Vector<DagNode*> &subterms);

  /**
   * To be called when an state is committed.
   */
  virtual int onCommitState(int dagNode,
			    StrategyStackManager::StackId stackId,
			    StrategicExecution* taskSibling,
			    StrategyTransitionGraph::TransitionType type,
			    int data) = 0;

  ~ModelCheckerSubtermTask();

private:
  //
  //	Virtual in StrategicTask.
  //
  Survival executionsExhausted(StrategicProcess* insertionPoint);

  // The following 3 contain the information about where the main pattern has been found
  // (and allow to replace the main pattern match with the result)
  shared_ptr<MatchSearchState> searchState;
  ExtensionInfo * extensionInfo;
  MatchSearchState::PositionIndex searchIndex;

protected:
  // A convenient function function for rebuilding the term
  int rebuild(const Vector<DagNode*> &subterms);

  // The strategic search object
  StrategicSearch &searchObject;
  // The subterm strategy
  SubtermStrategy* strategy;
  // The subterms of the matchrew execution
  Vector<DagNode*> subterms;
};

#endif
