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
//      Class for the random sample strategies.
//
#ifndef _sampleStrategy_hh_
#define _sampleStrategy_hh_
#include "strategyExpression.hh"
#include "cachedDag.hh"

class SampleStrategy : public StrategyExpression
{
public:
  enum Distribution {
    // bernoulli(<probability of 1>)
    BERNOULLI,
    // uniform(<lower bound>, <upper bound>)
    UNIFORM,
    UNIFORM_DISCRETE,
    // norm(<mean>, <standard deviation>)
    NORM,
    // gamma(<shape parameter>, <scale parameter>)
    GAMMA,
    // exp(<factor>)
    EXP,
    NUM_DISTRIBUTIONS
  };

  static const char* getName(Distribution distrib);
  static size_t getArgCount(Distribution dist);

  SampleStrategy(Term* variable, Distribution distribution, const Vector<Term*>& args, StrategyExpression* e);
  ~SampleStrategy();

  Term* getVariable() const;
  Distribution getDistribution() const;
  const Vector<CachedDag>& getArguments() const;
  StrategyExpression* getStrategy() const;

  bool check(VariableInfo& indices, const TermSet& boundVars);
  void process();

  StrategicExecution::Survival decompose(StrategicSearch& searchObject, DecompositionProcess* remainder);

private:
  Term* variable;
  Distribution distribution;
  Vector<CachedDag> argDags;
  StrategyExpression* strategy;
  Vector<int> contextSpec;
};

inline const char*
SampleStrategy::getName(Distribution distrib) {
  switch (distrib) {
    case BERNOULLI: return "bernoulli";
    case UNIFORM:
    case UNIFORM_DISCRETE: return "uniform";
    case NORM: return "norm";
    case GAMMA: return "gamma";
    case EXP: return "exp";
    default: return nullptr;
  }
}

inline Term*
SampleStrategy::getVariable() const
{
  return variable;
}

inline SampleStrategy::Distribution
SampleStrategy::getDistribution() const
{
  return distribution;
}

inline const Vector<CachedDag>&
SampleStrategy::getArguments() const
{
  return argDags;
}

inline StrategyExpression*
SampleStrategy::getStrategy() const
{
  return strategy;
}

#endif
