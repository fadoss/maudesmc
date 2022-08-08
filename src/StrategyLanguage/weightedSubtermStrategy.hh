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
//      Class of the strategy for rewriting of subterms (probabilistic version).
//
#ifndef _weightedSubtermStrategy_hh_
#define _weightedSubtermStrategy_hh_
#include "subtermStrategy.hh"
#include "cachedDag.hh"

// Forward declaration
class SuccSymbol;

class WeightedSubtermStrategy : public SubtermStrategy
{
public:
  //
  // Creates a strategy object for rewriting of subterms.
  //
  // matchrew P such that C by P1 using S1, ..., Pn using Sn.
  //
  // @param patternTerm The main pattern term (P).
  // @param depth Whether to match at top without extension (-1), with it (0) or all
  //	the way down to the leaf nodes (UNBOUNDED).
  // @param condition The condition (C).
  // @param patterns Subpattern terms (P1, ..., Pn)
  // @param strategies Strategies to rewrite the matched subterms (S1, ..., Sn)
  //
  WeightedSubtermStrategy(Term* patternTerm,
			  int depth,
			  const Vector<ConditionFragment*>& condition,
			  const Vector<Term*>& patterns,
			  const Vector<StrategyExpression*>& strategies,
			  Term* weight);

  bool check(VariableInfo& indices, const TermSet& boundVars);
  void process();

  Term* getWeight() const;

  StrategicExecution::Survival decompose(StrategicSearch& searchObject, DecompositionProcess* remainder);

private:
  CachedDag weight;
  SuccSymbol* succSymbol;
  bool needsFloating;
  Vector<int> weightContextSpec;
};

inline Term*
WeightedSubtermStrategy::getWeight() const
{
  return weight.getTerm();
}

#endif
