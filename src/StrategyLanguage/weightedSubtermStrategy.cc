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
//      Implementation for the rewriting of subterms strategy (probabilistic version).
//

//	utility stuff
#include "macros.hh"
#include "vector.hh"

//      forward declarations
#include "interface.hh"
#include "core.hh"
#include "higher.hh"
#include "strategyLanguage.hh"

//	interface class definitions
#include "symbol.hh"
#include "dagNode.hh"
#include "extensionInfo.hh"

//	higher class definitions
#include "matchSearchState.hh"

//	variable class definitions
#include "variableDagNode.hh"

//	strategy language class definitions
#include "decompositionProcess.hh"
#include "strategicSearch.hh"
#include "weightedSubtermStrategy.hh"
#include "subtermProcess.hh"
#include "termBag.hh"
#include "choiceStrategy.hh"

// We share the random number generator with the choice strategy
#include <random>

extern std::mt19937_64 choice_random_generator;

WeightedSubtermStrategy::WeightedSubtermStrategy(Term* patternTerm,
						 int depth,
						 const Vector<ConditionFragment*>& condition,
						 const Vector<Term*>& patterns,
						 const Vector<StrategyExpression*>& strategies,
						 Term* weight)
  : SubtermStrategy(patternTerm, depth, condition, patterns, strategies)
  , weight(weight)
  , needsFloating(false)
{
}

void
WeightedSubtermStrategy::process()
{
  SubtermStrategy::process();

  // Prepare the weight DAG
  weight.prepare();
  weight.getDag()->computeBaseSortForGroundSubterms(false);
}

bool
WeightedSubtermStrategy::check(VariableInfo& indices, const TermSet& boundVars)
{
  if (!SubtermStrategy::check(indices, boundVars))
    return false;

  // Index and check the variables in the weight term
  VariableInfo vinfo;
  weight.getTerm()->indexVariables(vinfo);
  weight.normalize();

  // Build the index translation recipe for the substitution
  // to be applied on the weight term
  int nrWeightVars = vinfo.getNrRealVariables();

  weightContextSpec.resize(nrWeightVars);

  for (int i = 0; i < nrWeightVars; ++i)
    {
      Term* var = vinfo.index2Variable(i);

      // Variable from the outer context
      if (boundVars.term2Index(var) != NONE)
	{
	  int outerIndex = indices.variable2Index(static_cast<VariableTerm*>(var));
	  weightContextSpec[i] = -(outerIndex + 1);
	}
      else
	{
	  int innerIndex = mainPattern.variable2Index(static_cast<VariableDagNode*>(
			    static_cast<VariableTerm*>(var)->term2Dag()));

          // Variable from the matching substitution
	  if (innerIndex != NONE)
	    weightContextSpec[i] = innerIndex;

          // Unbound variable
          else
	    {
	      IssueWarning(*var << ": unbound variable " << QUOTE(var) <<
			   " in a weight of the matchrew operator.");
	      return false;
	    }
	}
    }

  //
  // Type check the weights and decide whether integers or floating-point
  // numbers are expected.
  //
  ConnectedComponent* floatKind;
  ConnectedComponent* natKind;
  Term* weightTerm = weight.getTerm();

  ChoiceStrategy::getNumericalKinds(weightTerm->symbol()->getModule(),
				    natKind, floatKind, succSymbol);

  ConnectedComponent* argComponent = weightTerm->symbol()->getRangeSort()->component();
  if (argComponent == floatKind)
    needsFloating = true;

  else if (argComponent != natKind) {
    IssueWarning(*weightTerm << ": weight " << QUOTE(weightTerm)
		 << " is not of a numerical type.");
    return false;
  }

  return true;
}

template<typename T>
inline void deleteAll(const Vector<T*> &v)
{
  // Helper function for readability
  for (T* elem : v)
    delete elem;
}

StrategicExecution::Survival
WeightedSubtermStrategy::decompose(StrategicSearch& searchObject, DecompositionProcess* remainder)
{
  // Instantiates a search for the main pattern into the subject DAG node
  RewritingContext* context = searchObject.getContext();
  RewritingContext* newContext = context->makeSubcontext(searchObject.getCanonical(remainder->getDagIndex()));
  MatchSearchState* state = new MatchSearchState(newContext, &mainPattern, MatchSearchState::GC_CONTEXT |
						 MatchSearchState::GC_SUBSTITUTION, 0, getDepth());

  VariableBindingsManager::ContextId outerBinds = remainder->getOwner()->getVarsContext();

  // Applies the bindings of the outer context to the main pattern and condition
  if (!indexTranslation.isNull())
    {
      Vector<Term*> vars;
      Vector<DagRoot*> values;
      searchObject.buildInitialSubstitution(outerBinds, mainPattern, indexTranslation, vars, values);
      state->setInitialSubstitution(vars, values);
    }

  // Find all matches and select one according to their weights
  // (we need to keep copies of the matching information since the
  // search will reuse these attributes when searching again)
  Vector<ExtensionInfo*> extensionInfos;
  Vector<Substitution*> substitutions;
  Vector<int> positionIndices;

  Vector<unsigned long> iweights;
  Vector<double> fweights;

  while (state->findNextMatch())
   {
      ExtensionInfo* extensionInfo = state->getExtensionInfo();
      Substitution* subs = state->getContext();

      // If whole term was matched, we can get rid of the extension info
      if (extensionInfo)
	extensionInfo = !extensionInfo->matchedWhole() ? extensionInfo->makeClone() : nullptr;

      extensionInfos.append(extensionInfo);
      Substitution* subsCopy = new Substitution(subs->nrFragileBindings());
      subsCopy->copy(*subs);
      substitutions.append(subsCopy);
      positionIndices.append(state->getPositionIndex());

      // Instantiate the weight with the context and matching variables
      DagNode* iweight = weight.getTerm()->ground() ? weight.getDag()
		       : searchObject.instantiate(outerBinds, *subs,
						  weightContextSpec, weight.getDag());

      // Evaluate the weight as a number
      double newfWeight;
      unsigned long newiWeight;

      if (!ChoiceStrategy::evaluateWeight(iweight, searchObject, succSymbol, newfWeight,
					  newiWeight, needsFloating))
	{
	   IssueWarning(*weight.getTerm() << ": the weight " << QUOTE(iweight)
			<< " is not reduced to a number.");
	   deleteAll(extensionInfos);
           deleteAll(substitutions);
	   delete state;
	   return StrategicExecution::DIE;
	}
      if (needsFloating)
	fweights.append(newfWeight);
      else
	iweights.append(newiWeight);
   }

  // Select one of the matches according to their weights
  int choice = needsFloating ? ChoiceStrategy::chooseFloating(fweights)
                             : ChoiceStrategy::chooseInteger(iweights);

  // Fail if there are no options with positive weight
  if (choice < 0)
    {
      deleteAll(substitutions);
      deleteAll(extensionInfos);
      delete state;
      return StrategicExecution::DIE;
    }

  // Select the matching information of the selected match
  // and delete all other matches
  Substitution* substitution = nullptr;
  ExtensionInfo* extension = nullptr;

  swap(substitution, substitutions[choice]);
  swap(extension, extensionInfos[choice]);
  deleteAll(substitutions);
  deleteAll(extensionInfos);

  const Vector<int>& contextSpec = getContextSpec();

  // The variable context inside the matchrew mixes variables of
  // the outer context with those from the matching
  VariableBindingsManager::ContextId innerBinds = contextSpec.empty()
	    ? VariableBindingsManager::EMPTY_CONTEXT
	    : searchObject.openContext(outerBinds, *substitution, contextSpec);

  // The weighted subterm strategy creates a single subterm task
  // for the match that has been selected at random
  SubtermProcess::newSubtermTask(searchObject, this,
				 shared_ptr<MatchSearchState>(state),
				 substitution, extension,
				 positionIndices[choice],
				 remainder->getPending(),
				 innerBinds,
				 remainder, remainder);

  // remainder should die because the subterm task gets in charge of its pending work
  return StrategicExecution::DIE;
}
