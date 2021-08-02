/*

    This file is part of the Maude 3 probabilistic strategy language extension.

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
//      Implementation for the random sample strategy.
//

//	utility stuff
#include "macros.hh"
#include "vector.hh"

//      forward declarations
#include "interface.hh"
#include "core.hh"
#include "strategyLanguage.hh"
#include "NA_Theory.hh"
#include "builtIn.hh"

//	core class definitions
#include "rewritingContext.hh"
#include "variableInfo.hh"

//	builtin class definitions
#include "floatSymbol.hh"
#include "floatDagNode.hh"

//	strategy language class definitions
#include "sampleStrategy.hh"
#include "decompositionProcess.hh"
#include "strategicSearch.hh"
#include "callTask.hh"

#include <random>

// Share the random generator with the choice operator
extern mt19937_64 choice_random_generator;

inline size_t
SampleStrategy::getArgCount(Distribution dist)
{
  switch (dist) {
    case BERNOULLI: return 1;
    case UNIFORM: return 2;
    case NORM: return 2;
    case GAMMA: return 2;
    case EXP: return 1;
    default: return 0;
  }
}

SampleStrategy::SampleStrategy(Term* variable, Distribution distribution, const Vector<Term*>& args, StrategyExpression* e)
  : variable(variable), distribution(distribution), argDags(args.size()), strategy(e)
{
  Assert(variable != nullptr, "no variable");
  Assert(e != nullptr, "no strategy");

  size_t nrArgs = args.size();

  for (size_t i = 0; i < nrArgs; ++i)
    argDags[i].setTerm(args[i]);

  variable->normalize(true);
}

SampleStrategy::~SampleStrategy()
{
  variable->deepSelfDestruct();
  delete strategy;
}

bool
SampleStrategy::check(VariableInfo& indices, const TermSet& boundVars)
{
  // Check number of arguments for distribution
  size_t expectedArgs = getArgCount(distribution);

  if (argDags.size() != expectedArgs)
    {
      IssueWarning(*variable << ": wrong number of arguments for "
		   << QUOTE(getName(distribution))  << " distribution (expected "
		   << expectedArgs << ", got " << argDags.size() << ").");
      return false;
    }

  // Index and check the variables in the weights
  for (CachedDag& dag : argDags)
    {
      dag.getTerm()->indexVariables(indices);
      dag.normalize();

      const NatSet& occurSet = dag.getTerm()->occursBelow();

      for (int index : occurSet)
        {
	  Term* var = indices.index2Variable(index);

	  if (boundVars.term2Index(var) == NONE)
            {
	      IssueWarning(*var << ": unbound variable " << QUOTE(var) <<
			   " in the distribution parameter of the sample operator.");
	      return false;
            }
        }
    }

  // Check whether the sample variable is actually a variable
  VariableTerm* sampleVariable = dynamic_cast<VariableTerm*>(variable);

  if (sampleVariable == nullptr)
    {
      IssueWarning(*variable << ": the sample destination " << QUOTE(variable) << " is not a variable.");
      return false;
    }

  // Index the variable in the nested expression
  VariableInfo vinfo;
  TermSet boundNew = boundVars;

  sampleVariable->indexVariables(vinfo);
  boundNew.insert(variable);

  // Check and index the variables in the strategy
  if (!strategy->check(vinfo, boundNew))
    return false;

  int nrInnerContextVars = vinfo.getNrRealVariables();
  contextSpec.expandTo(nrInnerContextVars);

  for (int i = 0; i < nrInnerContextVars; i++)
    {
      VariableTerm* var = static_cast<VariableTerm*>(vinfo.index2Variable(i));

      if (var == sampleVariable)
	contextSpec[i] = sampleVariable->getIndex();
      else
	contextSpec[i] = - indices.variable2Index(var) - 1;	// the complemented outer index
    }

  return true;
}

void
SampleStrategy::process()
{
  // Fill in the variable sort info
  variable->symbol()->fillInSortInfo(variable);

  // Recursively process the nested formula
  strategy->process();
}

StrategicExecution::Survival
SampleStrategy::decompose(StrategicSearch& searchObject, DecompositionProcess* remainder)
{
  // The interruption of the strategic search when a state is visited twice
  // should be disabled in this case, since a second evaluation of the same
  // state may produce a different random result. Moreover, strategies should
  // be deterministic for the statistical analysis to be sound.

  if (searchObject.getSkipSeenStates())
    searchObject.setSkipSeenStates(false);

  VariableBindingsManager::ContextId outerBinds = remainder->getOwner()->getVarsContext();
  RewritingContext* context = searchObject.getContext();
  size_t nrArgs = argDags.size();

  // Evaluate the arguments of the distribution
  FloatDagNode* floatArgDag;
  Vector<double> doubleArgs(nrArgs);

  for (size_t i = 0; i < nrArgs; i++)
    {
      // Arguments are instantiated, ...
      DagNode* argDag = argDags[i].getDag();
      argDag = argDags[i].getTerm()->ground() ? argDag
		: searchObject.instantiate(outerBinds, argDag);

      // ...reduced equationally, ...
      RewritingContext* redContext = context->makeSubcontext(argDag);
      redContext->reduce();
      context->transferCountFrom(*redContext);

      // ...and converted to a floating-point number if possible
      floatArgDag = dynamic_cast<FloatDagNode*>(redContext->root());

      if (floatArgDag == nullptr)
	{
	  IssueWarning("the argument " << QUOTE(floatArgDag)
		       << " passed to the sample operator is not a floating-point number.");
	  delete redContext;
	  return StrategicExecution::DIE;
	}
      else
	doubleArgs[i] = floatArgDag->getValue();

      delete redContext;
    }

  // Sample the requested value
  double sampled = 0.0;

  switch (distribution)
    {
      case BERNOULLI:
	sampled = bernoulli_distribution(doubleArgs[0])(choice_random_generator);
	break;
      case UNIFORM:
	sampled = uniform_real_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case NORM:
	sampled = normal_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case GAMMA:
	sampled = gamma_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case EXP:
	sampled = exponential_distribution<double>(doubleArgs[0])(choice_random_generator);
	break;
      case NUM_DISTRIBUTIONS:
	CantHappen("bad distribution");
    }

  // Open a variable context where the sample variable is defined
  int index = static_cast<VariableTerm*>(variable)->getIndex();
  Substitution sb(index + 1);
  sb.bind(index, new FloatDagNode(safeCast(FloatSymbol*, floatArgDag->symbol()), sampled));

  VariableBindingsManager::ContextId innerBinds = contextSpec.empty()
	      ? VariableBindingsManager::EMPTY_CONTEXT
	      : searchObject.openContext(outerBinds, sb, contextSpec);

  // A call task is created to enclose the variable context
  (void) new CallTask(searchObject,
		      remainder->getDagIndex(),
		      nullptr,
		      strategy,
		      remainder->getPending(),
		      innerBinds,
		      remainder,
		      remainder);

  return StrategicExecution::DIE;
}
