/**
 * @file strategicSimulation.hh
 *
 * Instance of StrategicSearch adapted to simulations.
 */
 
#ifndef _strategicSimulation_hh_
#define _strategicSimulation_hh_

#include "strategicSearch.hh"

class StrategicSimulation
  : private StrategicSearch
{
  NO_COPYING(StrategicSimulation);

public:
  StrategicSimulation(RewritingContext* initial, StrategyExpression* strategy);

  int findNextResult();

  DagNode* getStateTerm(int stateNr) const;

  Int64 getRewriteCount();

private:
  DagNode* findNextSolution() override;
  int findNextSolutionIndex();

  StrategyExpression* strategy;
  StrategicProcess* nextToRun;
};

inline DagNode*
StrategicSimulation::getStateTerm(int stateNr) const
{
  return getCanonical(stateNr);
}

inline Int64
StrategicSimulation::getRewriteCount()
{
  return getContext()->getTotalCount();
}

#endif
