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
//      Class for random choice strategies.
//
#ifndef _choiceStrategy_hh_
#define _choiceStrategy_hh_
#include "strategyExpression.hh"
#include "cachedDag.hh"
#include "variableBindingsManager.hh"

// Forward declaration
class SuccSymbol;

class ChoiceStrategy : public StrategyExpression
{
public:
  ChoiceStrategy(const Vector<Term*>& weights, const Vector<StrategyExpression*>& strategies);
  ~ChoiceStrategy();

  const Vector<StrategyExpression*>& getStrategies() const;
  const Vector<CachedDag>& getWeights() const;

  bool check(VariableInfo& indices, const TermSet& boundVars);
  void process();

  StrategicExecution::Survival decompose(StrategicSearch& searchObject, DecompositionProcess* remainder);

  // Obtain the numerical kinds (Float and Nat) in the given module (if defined)
  static void getNumericalKinds(Module* mod,
				ConnectedComponent*& natKind,
				ConnectedComponent*& floatKind,
				SuccSymbol*& succSymbol);

  // Evaluate a weight in the given context
  static bool evaluateWeight(DagNode* weight,
			     StrategicSearch& searchObject,
			     SuccSymbol* succSymbol,
			     double& fvalue,
			     unsigned long& ivalue,
			     bool useFloating = true);

  static int chooseInteger(const Vector<unsigned long>& ivalues);
  static int chooseFloating(const Vector<double>& fpvalues);

private:
  const Vector<StrategyExpression*> strategies;
  Vector<CachedDag> weightDags;
  Vector<unsigned long> ivalues;
  Vector<double> fpvalues;
  SuccSymbol* succSymbol;
};

inline const Vector<StrategyExpression*>&
ChoiceStrategy::getStrategies() const
{
  return strategies;
}

inline const Vector<CachedDag>&
ChoiceStrategy::getWeights() const
{
  return weightDags;
}

#endif
