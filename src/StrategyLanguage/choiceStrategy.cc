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
//      Implementation for the random choice strategy.
//

//	utility stuff
#include "macros.hh"
#include "vector.hh"

//      forward declarations
#include "interface.hh"
#include "core.hh"
#include "strategyLanguage.hh"
#include "NA_Theory.hh"

//	core class definitions
#include "rewritingContext.hh"
#include "variableInfo.hh"

//	builtin class definitions
#include "succSymbol.hh"
#include "floatSymbol.hh"
#include "floatDagNode.hh"

//	strategy language class definitions
#include "choiceStrategy.hh"
#include "decompositionProcess.hh"
#include "strategicSearch.hh"

// The rand function in the C standard may only provide random numbers
// in the range 0 to 32767, so the standard C++ library random is used
#include <random>

mt19937_64 choice_random_generator;

void
setChoiceSeed(unsigned long seed)
{
  choice_random_generator.seed(seed);
}

ChoiceStrategy::ChoiceStrategy(const Vector<Term*>& weights, const Vector<StrategyExpression*>& strategies)
  : strategies(strategies), weightDags(weights.size())
{
  Assert(!strategies.empty(), "no strategies");
  Assert(weights.size() == strategies.size(), "the numbers of weights and strategies differ");

  size_t nrStrategies = strategies.size();

  for (size_t i = 0; i < nrStrategies; ++i)
    weightDags[i].setTerm(weights[i]);
}

ChoiceStrategy::~ChoiceStrategy()
{
  for (StrategyExpression* e : strategies)
    delete e;
}

void
ChoiceStrategy::getNumericalKinds(Module* mod,
                                  ConnectedComponent*& natKind,
                                  ConnectedComponent*& floatKind,
                                  SuccSymbol*& succSymbol)
{
  // Uses the dirty trick of exploring all symbols of the module
  // for the kinds of Nat and Float.

  floatKind = nullptr;
  natKind = nullptr;
  succSymbol = nullptr;

  const Vector<Symbol*>& symbols = mod->getSymbols();
  size_t nrSymbols = symbols.size();
  Symbol* wantedSymbol = nullptr;

  // Find the Float kind by looking for a FloatSymbol operator
  for (size_t i = 0; i < nrSymbols && wantedSymbol == nullptr; ++i)
    wantedSymbol = dynamic_cast<FloatSymbol*>(symbols[i]);

  if (wantedSymbol)
    floatKind = wantedSymbol->getRangeSort()->component();

  // Find the Nat kind by looking for the SuccSymbol operator
  for (size_t i = 0; i < nrSymbols && succSymbol == nullptr; ++i)
    succSymbol = dynamic_cast<SuccSymbol*>(symbols[i]);

  if (succSymbol)
    natKind = succSymbol->getRangeSort()->component();
}


bool
ChoiceStrategy::check(VariableInfo& indices, const TermSet& boundVars)
{
  // Index and check the variables in the weights
  for (CachedDag& dag : weightDags)
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
			   " in a weight of the choice operator.");
	      return false;
            }
        }
    }

  //
  // Type check the weights and decide whether integers or floating-point
  // numbers are expected. This includes the dirty trick of exploring all
  // symbols of the module for the kinds of these specific sorts.
  //
  bool needsFloating = false;
  ConnectedComponent* natKind;
  ConnectedComponent* floatKind;

  getNumericalKinds(weightDags[0].getTerm()->symbol()->getModule(),
                    natKind, floatKind, succSymbol);

  for (const CachedDag& cdag : weightDags)
    {
      ConnectedComponent* argComponent = cdag.getTerm()->symbol()->getRangeSort()->component();
      if (argComponent == floatKind)
	{
	  if (!needsFloating)
	    needsFloating = true;
	}
      else if (argComponent != natKind) {
	IssueWarning(*cdag.getTerm() << ": weight " << QUOTE(cdag.getTerm())
		     << " is not of a numerical type.");
	return false;
      }
    }

  if (needsFloating)
    fpvalues.resize(strategies.size());
  else
    ivalues.resize(strategies.size());

  // Check and index the variables in the strategies
  for (StrategyExpression* e : strategies)
    if (!e->check(indices, boundVars))
      return false;

  return true;
}

void
ChoiceStrategy::process()
{
  for (StrategyExpression* e : strategies)
    e->process();
}

int
ChoiceStrategy::chooseFloating(const Vector<double>& fpvalues)
{
  if (all_of(fpvalues.begin(), fpvalues.end(), [](double f) { return f == 0; }))
    return NONE;

  // Choose one strategy according to their weights
  return discrete_distribution<int>(fpvalues.begin(), fpvalues.end())(choice_random_generator);
}

int
ChoiceStrategy::chooseInteger(const Vector<unsigned long>& ivalues)
{
  unsigned long total = accumulate(ivalues.begin(), ivalues.end(), 0);

  if (total == 0)
    return NONE;

  // Choose one strategy according to their weights using a random number
  unsigned long rnd = uniform_int_distribution<unsigned long>(0, total - 1)(choice_random_generator);
  int choice;
  int nrChoices = ivalues.size();

  for (choice = 0; choice < nrChoices - 1; ++choice)
    {
      if (rnd < ivalues[choice])
	break;
      else
	rnd -= ivalues[choice];
    }

  return choice;
}

bool
ChoiceStrategy::evaluateWeight(DagNode* weight,
			       StrategicSearch& searchObject,
			       SuccSymbol* succSymbol,
			       double& fvalue,
			       unsigned long& ivalue,
			       bool useFloating)
{
  // Reduce the weight term
  RewritingContext* weightContext = searchObject.getContext()->makeSubcontext(weight);
  weightContext->reduce();
  searchObject.getContext()->transferCountFrom(*weightContext);

  // Extract the numerical value from the node (if possible)
  if (useFloating)
    {
      if (FloatDagNode* floatDag = dynamic_cast<FloatDagNode*>(weightContext->root()))
	fvalue = floatDag->getValue();
      else if (succSymbol->isNat(weightContext->root()))
	fvalue = succSymbol->getNat(weightContext->root()).get_ui();
      else
        {
	  delete weightContext;
	  return false;
	}
    }
  else
    {
      if (succSymbol->isNat(weightContext->root()))
	ivalue = succSymbol->getNat(weightContext->root()).get_ui();
      else
        {
	  delete weightContext;
	  return false;
	}
    }

  delete weightContext;
  return true;
}

StrategicExecution::Survival
ChoiceStrategy::decompose(StrategicSearch& searchObject, DecompositionProcess* remainder)
{
  // The interruption of the strategic search when a state is visited twice
  // should be disabled in this case, since a second evaluation of the same
  // state may produce a different random result. Moreover, strategies should
  // be deterministic for the statistical analysis to be sound.

  if (searchObject.getSkipSeenStates())
    searchObject.setSkipSeenStates(false);

  int nrStrategies = strategies.size();
  VariableBindingsManager::ContextId vctx = remainder->getOwner()->getVarsContext();

  // Evaluate the weights in the current variable context
  for (int i = 0; i < nrStrategies; ++i)
    {
      DagNode* weight = weightDags[i].getTerm()->ground()
			  ? weightDags[i].getDag()
			  : searchObject.instantiate(vctx, weightDags[i].getDag());

      double fpvalue;
      unsigned long ivalue;

      if (!evaluateWeight(weight, searchObject, succSymbol, fpvalue, ivalue, ivalues.isNull()))
	{
	  IssueWarning(*weightDags[i].getTerm() << ": the weight " << QUOTE(weight)
		       << " is not reduced to a number.");
	  return StrategicExecution::DIE;
	}

      if (ivalues.isNull())
	fpvalues[i] = fpvalue;
      else
	ivalues[i] = ivalue;
    }

  // Make a choice
  int choice = ivalues.isNull() ? chooseFloating(fpvalues) : chooseInteger(ivalues);

  if (choice == NONE)
    return StrategicExecution::DIE;

  // We directly call the decompose method of the choice instead of creating
  // a DecompositionProcess for it
  return strategies[choice]->decompose(searchObject, remainder);
}
