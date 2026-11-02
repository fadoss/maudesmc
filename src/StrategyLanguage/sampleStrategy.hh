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

#include <vector>

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
    // binomial(<number of trials>, <probability>)
    BINOMIAL,
    // negbinomial(<number of trial successes>, <probability>)
    NEGBINOMIAL,
    // geometric(<probability>)
    GEOMETRIC,
    // poisson(<mean>)
    POISSON,
    // cauchy(<location>, <scale>)
    CAUCHY,
    // weibull(<shape>, <scale>)
    WEIBULL,
    // lognormal(<log-scale>, <shape>)
    LOGNORMAL,
    // chi2(<degrees of freedom>)
    CHI2,
    // studentt(<degrees of freedom>)
    STUDENTT,
    NUM_DISTRIBUTIONS
  };

  // Signature of a distribution
  struct DistributionSignature {
    enum Type { FLOAT, INTEGER, BOOL };
    const char* name;
    std::vector<Type> args;
    Type result;
  };

  static const DistributionSignature& getSignature(Distribution distrib);

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

  // For building the result
  Symbol* resultSymbol;
  CachedDag trueTerm;
  CachedDag falseTerm;
};

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
