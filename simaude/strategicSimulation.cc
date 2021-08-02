/**
 * @file strategicSimulation.cc
 *
 * Instance of StrategicSearch adapted to simulations.
 */

//	utility stuff
#include "macros.hh"
#include "vector.hh"

//      forward declarations
#include "interface.hh"
#include "core.hh"
#include "strategyLanguage.hh"

//      core class definitions
#include "rewritingContext.hh"

//	strategy language class definitions
#include "strategicSimulation.hh"
#include "decompositionProcess.hh"

StrategicSimulation::StrategicSimulation(RewritingContext* initial, StrategyExpression* strategy)
 : StrategicSearch(initial, strategy), strategy(strategy)
{
  nextToRun = new DecompositionProcess(insert(initial->root()),
				       push(EMPTY_STACK, strategy),
				       getDummyExecution(),
				       0);
}

int
StrategicSimulation::findNextSolutionIndex()
{
  // This method is an exact copy of FairStrategicSearch::findNextSolution,
  // but it returns an index to the hash table instead of a DAG node

  solutionIndex = NONE;
  while (!exhausted)
    {
      //
      //	Run can delete any process except nextToRun, which must
      //	request deletion by returning DIE.
      //
      Survival s = nextToRun->run(*this);
      //
      //	t will always be a valid process, but might be equal to
      //	nextToRun in the degenerate case.
      //
      StrategicProcess* t = nextToRun->getNextProcess();
      Assert(s == SURVIVE || t != nextToRun || exhausted, "ran out of processes without exhausted being set");
      //
      //	Now safe to delete nextToRun if it requested deletion.
      //
      if (s == DIE)
	delete nextToRun;
      //
      //	Now safe to abort.
      //
      if (RewritingContext::getTraceStatus() && initial->traceAbort())
	break;
      //
      //	t may not be valid anymore, but in this case we should be
      //	exhausted.
      //
      nextToRun = t;
      //
      //	If the run triggered a solution we're done.
      //
      if (solutionIndex != NONE)
	return solutionIndex;
    }
  return NONE;
}

// Dirty hacks to access some private members
// (not to modify Maude for the moment)

template<typename Tag, typename Tag::type M>
struct PrivateHack {
	friend typename Tag::type get(Tag) {
		return M;
	}
};

struct HackSeenSet {
	typedef set<pair<int, int>> StrategicTask::* type;
	friend type get(HackSeenSet);
};

template struct PrivateHack<HackSeenSet, &StrategicTask::seenSet>;

int
StrategicSimulation::findNextResult()
{
  int nextSolution = findNextSolutionIndex();

  // Restart the strategy search when the previous one does not provide any
  // solution. Since we are simulating with potentially random behavior,
  // the result of the next execution may produce a different result.

  if (nextSolution == NONE) {
    auto &seenSet = (*this).*get(HackSeenSet());
    nextToRun = new DecompositionProcess(insert(initial->root()),
					 push(EMPTY_STACK, strategy),
					 getDummyExecution(),
					 0);
    seenSet.clear();
    exhausted = false;
    nextSolution = findNextSolutionIndex();
  }

  return nextSolution;
}

DagNode*
StrategicSimulation::findNextSolution()
{
  // This findNextSolution method that returns a DagNode needs to be defined
  // because it is pure virtual in StrategicSearch, but it will not be used.

  int nextSolution = findNextSolutionIndex();
  return nextSolution == NONE ? nullptr : getCanonical(nextSolution);
}

