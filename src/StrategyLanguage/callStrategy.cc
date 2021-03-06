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
//      Implementation for call invocation strategy.
//

//	utility stuff
#include "macros.hh"
#include "vector.hh"

//      forward declarations
#include "interface.hh"
#include "core.hh"
#include "strategyLanguage.hh"

//	interface class definitions
#include "term.hh"
#include "lhsAutomaton.hh"
#include "subproblem.hh"

//	core class definitions
#include "rewriteStrategy.hh"
#include "strategyDefinition.hh"
#include "module.hh"
#include "substitution.hh"
#include "rewritingContext.hh"

//	higher class definitions
#include "searchState.hh"
#include "strategyTransitionGraph.hh"

//	strategy language class definitions
#include "strategicSearch.hh"
#include "callStrategy.hh"
#include "decompositionProcess.hh"
#include "callProcess.hh"
#include "callTask.hh"

CallStrategy::CallStrategy(RewriteStrategy* strat, Term* term)
  : strategy(strat),
    callTerm(term)
{
  Assert(term != 0, "term cannot be null");

  callTerm.normalize();
}

bool
CallStrategy::check(VariableInfo& indices, const TermSet& boundVars)
{
  // Index the call term variables
  Term* call = callTerm.getTerm();
  call->indexVariables(indices);

  // Check that they are properly bound
  const NatSet& occurSet = call->occursBelow();

  for (int index : occurSet)
    {
      Term * var = indices.index2Variable(index);

      if (boundVars.term2Index(var) == NONE)
        {
	  IssueWarning(*call << ": unbound variable " <<
		       QUOTE(var) << " in strategy call argument.");
	  return false;
	}
    }

  return true;
}

void
CallStrategy::process()
{
  // computeBaseSortForGroundSubterms must be called to set the ground flags
  // of the call termand its subterms. Otherwise, instantiate will fail when
  // the debugging checks are activated.
  callTerm.getDag()->computeBaseSortForGroundSubterms(false);
}

inline bool
tailCall(DecompositionProcess* remainder, StrategyTransitionGraph* transitionGraph)
{
  // Due to the optimized way the call task are managed in the usual
  // strategies execution, we do not treat as tail call those whose
  // caller is not inside a CallTask.
  return remainder->getPending() == StrategyStackManager::EMPTY_STACK
    && (transitionGraph != 0 || dynamic_cast<CallTask*>(remainder->getOwner()) != 0);
}

StrategicExecution::Survival
CallStrategy::decompose(StrategicSearch& searchObject, DecompositionProcess* remainder)
{
  RewritingContext* context = searchObject.getContext();
  StrategyTransitionGraph* transitionGraph = remainder->getOwner()->getTransitionGraph();

  if (strategy->getDefinitions().empty())
    {
      IssueAdvisory(strategy << " undefined strategy.");
      return StrategicExecution::DIE;
    }

  //
  // Strategies without arguments and defined by a single unconditional
  // definition are handled faster than the other strategies.
  //
  if (strategy->isSimple())
    {
      StrategyDefinition* definition = strategy->getDefinitions()[0];

      if (context->getTraceStatus())
	context->traceStrategyCall(definition,
				   callTerm.getDag(),
				   searchObject.getCanonical(remainder->getDagIndex()),
				   context);

      //
      // For tail calls, the strategy expression can even simply be pushed
      // on top of the stack (unless starting an opaque call). Otherwise, it
      // creates a call task to run the strategy code.
      //
      // Both actions are correct no matter if the call is tail, but the
      // advantages of pushing the strategy on top of the stack are lost
      // when it is not.
      //
      bool isOpaque = transitionGraph != 0 && transitionGraph->isOpaque(strategy->id());
      bool isTail = tailCall(remainder, transitionGraph);
      if (isTail && !isOpaque)
	{
	  remainder->pushStrategy(searchObject, definition->getRhs());
	  // Avoid cycling on strategy definitions like st := st
	  if (transitionGraph != 0
	      && !transitionGraph->onCheckpoint(remainder->getDagIndex(), remainder,
						remainder->getPending(), remainder))
	    return StrategicExecution::DIE;
	  return StrategicExecution::SURVIVE;
	}

      StrategicTask* callTask = new CallTask(searchObject,
			  remainder->getDagIndex(),
			  strategy,
			  definition->getRhs(),
			  remainder->getPending(),
			  VariableBindingsManager::EMPTY_CONTEXT,
			  remainder,
			  remainder);

      // Informs the model checker about the new tail call. Then, the new call
      // task can share its seen map with other tail call tasks using the same
      // variable context.
      if (transitionGraph != 0 && isTail)
	{
	  transitionGraph->getContextGroup(remainder->getOwner());
	  transitionGraph->onStrategyCall(callTask, VariableBindingsManager::EMPTY_CONTEXT);
	  // Since this is a opaque call, the cycle prevention mechanism of the model
	  // checker does not have to take care about recursive calls, so we do not call
	  // onCheckPoint
	}

      return StrategicExecution::DIE;
    }

  //
  // In the general case, we may need to explore different matches and
  // substitutions from conditions. We create a CallProcess to explore
  // them all.
  //

  // Instantiates the call arguments with the current variable substitution and reduce them
  VariableBindingsManager::ContextId varBinds = remainder->getOwner()->getVarsContext();
  DagNode* instantiatedCall = callTerm.getTerm()->ground() ? callTerm.getDag() :
				searchObject.instantiate(varBinds, callTerm.getDag());
  RewritingContext* callContext = context->makeSubcontext(instantiatedCall);
  callContext->reduce();
  searchObject.getContext()->transferCountFrom(*callContext);

  (void) new CallProcess(strategy,
			 callContext,
			 remainder->getDagIndex(),
			 remainder->getPending(),
			 tailCall(remainder, transitionGraph),
			 remainder,
			 remainder);

  return StrategicExecution::DIE;	// request deletion of DecompositionProcess
}

bool
CallStrategy::equal(const StrategyExpression& other) const
{
  //
  // Equality of call strategy is completed by considering equal calls
  // without arguments to the same strategy. Extending that to calls with
  // arguments would be convenient, but its variables indices may differ
  // and this will cause trouble in execution.
  //

  const CallStrategy* callOther = dynamic_cast<const CallStrategy*>(&other);
  return this == &other || (callOther != 0
	  && strategy->id() == callOther->strategy->id()
	  && strategy->arity() == 0 && callOther->strategy->arity() == 0);
}
