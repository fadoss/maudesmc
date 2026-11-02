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
#include "builtIn.hh"

//	core class definitions
#include "rewritingContext.hh"
#include "variableInfo.hh"

//	numeric class definitions
#include "floatSymbol.hh"
#include "floatOpSymbol.hh"
#include "floatDagNode.hh"
#include "minusSymbol.hh"
#include "succSymbol.hh"

//	strategy language class definitions
#include "sampleStrategy.hh"
#include "decompositionProcess.hh"
#include "strategicSearch.hh"
#include "callTask.hh"

#include <random>

// Share the random generator with the choice operator
extern mt19937_64 choice_random_generator;

// Signature of probability distributions
constexpr auto FLOAT = SampleStrategy::DistributionSignature::FLOAT;
constexpr auto INTEGER = SampleStrategy::DistributionSignature::INTEGER;
constexpr auto BOOL = SampleStrategy::DistributionSignature::BOOL;

const SampleStrategy::DistributionSignature signatures[SampleStrategy::NUM_DISTRIBUTIONS] = {
	{"bernoulli", {FLOAT}, BOOL},
	{"uniform", {FLOAT, FLOAT}, FLOAT},
	{"uniform", {INTEGER, INTEGER}, INTEGER},
	{"norm", {FLOAT, FLOAT}, FLOAT},
	{"gamma", {FLOAT, FLOAT}, FLOAT},
	{"exp", {FLOAT}, FLOAT},
	{"binomial", {INTEGER, FLOAT}, INTEGER},
	{"negbinomial", {INTEGER, FLOAT}, INTEGER},
	{"geometric", {FLOAT}, INTEGER},
	{"poisson", {FLOAT}, INTEGER},
	{"cauchy", {FLOAT, FLOAT}, FLOAT},
	{"weibull", {FLOAT, FLOAT}, FLOAT},
	{"lognormal", {FLOAT, FLOAT}, FLOAT},
	{"chi2", {INTEGER}, FLOAT},
	{"studentt", {INTEGER}, FLOAT},
};

const SampleStrategy::DistributionSignature&
SampleStrategy::getSignature(Distribution distribution)
{
	return signatures[distribution];
}

SampleStrategy::SampleStrategy(Term* variable, Distribution distribution, const Vector<Term*>& args, StrategyExpression* e)
  : variable(variable), distribution(distribution), argDags(args.size()), strategy(e), resultSymbol(nullptr)
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
  // Index and check the variables in the distribution parameters
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

  // Check whether the output variable has the appropriate sort
  // (at the object level, we already known it is in the appropriate kind)
  DistributionSignature::Type rtype = getSignature(distribution).result;

  for (Symbol* symbol : variable->symbol()->getModule()->getSymbols())
    {
      bool found = false;
      switch (rtype)
	{
	  case FLOAT: found = dynamic_cast<FloatSymbol*>(symbol) != nullptr; break;
	  case INTEGER: found = dynamic_cast<SuccSymbol*>(symbol) != nullptr; break;
	  case BOOL: found = dynamic_cast<FloatOpSymbol*>(symbol) != nullptr; break;
	}

      if (found)
	{
	  // Find the Bool constants through the order FloatOpSymbols (this is very hacky)
	  if (rtype == BOOL)
	    {
	      Vector<const char*> purposes;
	      Vector<Term*> terms;
	      symbol->getTermAttachments(purposes, terms);

	      if (purposes.size() < 2)
		continue;

	      trueTerm.setTerm((purposes[0][0] == 't' ? terms[0] : terms[1])->deepCopy());
	      falseTerm.setTerm((purposes[0][0] == 't' ? terms[1] : terms[0])->deepCopy());
	      trueTerm.normalize();
	      falseTerm.normalize();

	      symbol = terms[0]->symbol();
	    }

	  resultSymbol = symbol;

	  if (!leq(symbol->getRangeSort(), variable->symbol()->getRangeSort()))
	    {
	      IssueWarning(*variable << ": the sample destination " << QUOTE(variable)
		 << " does not have the appropiate sort for the "
		 << getSignature(distribution).name << " distribution.");
	      return false;
	    }

	  break;
	}
    }

  // Fail if we cannot find the appropriate symbol
  if (resultSymbol == nullptr)
    {
      IssueWarning(*variable << ": the module does not contain the appropiate sorts for the "
		   << getSignature(distribution).name << " distribution.");
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
  Vector<double> doubleArgs(nrArgs);
  Vector<long int> integerArgs(nrArgs);

  // Symbols to build the result term
  MinusSymbol* minusSymbol = nullptr;

  const DistributionSignature& signature = getSignature(distribution);

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

      // ...and converted to a number if possible
      bool isNumber = false;

      if (signature.args[i] == INTEGER)
	{
	  DagNode* topNode = redContext->root();
	  Symbol* topSymbol = topNode->symbol();

	  // Any constant is a zero for us
	  if (topSymbol->arity() == 0)
	    {
	      integerArgs[i] = 0;
	      isNumber = true;
	    }
	  // Negative integer
	  else if (MinusSymbol* minus = dynamic_cast<MinusSymbol*>(topSymbol))
	    {
	      if (minus->isNeg(topNode))
		{
		  mpz_class result;
		  minusSymbol = minus;
		  integerArgs[i] = minus->getNeg(topNode, result).get_si();
		  isNumber = true;
		}
	    }
	  // Natural number
	  else if (SuccSymbol* succ = dynamic_cast<SuccSymbol*>(topSymbol))
	    {
	      if (succ->isNat(topNode))
		{
		  integerArgs[i] = succ->getNat(topNode).get_si();
		  isNumber = true;
		}
	    }
	}
      else if (FloatDagNode* floatArgDag = dynamic_cast<FloatDagNode*>(redContext->root()))
	{
	  doubleArgs[i] = floatArgDag->getValue();
	  isNumber = true;
	}

      if (!isNumber)
	{
	  IssueWarning("the argument " << QUOTE(redContext->root())
		       << " passed to the sample operator does not reduce to a numeric constant of the right type.");
	  delete redContext;
	  return StrategicExecution::DIE;
	}

      delete redContext;
    }


  // Sample the requested value
  // (with some precondition check to avoid C++ undefined behavior)
  double sampled = 0.0;
  long int sampledInteger = 0;

  switch (distribution)
    {
      case BERNOULLI:
	if (doubleArgs[0] < 0.0 || doubleArgs[0] > 1.0)
	  return StrategicExecution::DIE;
	sampledInteger = bernoulli_distribution(doubleArgs[0])(choice_random_generator);
	break;
      case UNIFORM:
	if (doubleArgs[0] > doubleArgs[1])
	  return StrategicExecution::DIE;
	sampled = uniform_real_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case UNIFORM_DISCRETE:
	if (integerArgs[0] > integerArgs[1])
	  return StrategicExecution::DIE;
        sampledInteger = uniform_int_distribution<long int>(integerArgs[0], integerArgs[1])(choice_random_generator);
	break;
      case NORM:
	sampled = normal_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case GAMMA:
	sampled = gamma_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case EXP:
	if (doubleArgs[0] <= 0.0)
	  return StrategicExecution::DIE;
	sampled = exponential_distribution<double>(doubleArgs[0])(choice_random_generator);
	break;
      case BINOMIAL:
	if (integerArgs[0] < 0 || doubleArgs[0] < 0.0 || doubleArgs[0] > 1.0)
	  return StrategicExecution::DIE;
	sampledInteger = binomial_distribution<long int>(integerArgs[0], doubleArgs[0])(choice_random_generator);
	break;
      case NEGBINOMIAL:
	if (integerArgs[0] <= 0 || doubleArgs[0] <= 0.0 || doubleArgs[0] > 1.0)
	  return StrategicExecution::DIE;
	sampledInteger = negative_binomial_distribution<long int>(integerArgs[0], doubleArgs[0])(choice_random_generator);
	break;
      case GEOMETRIC:
	if (doubleArgs[0] <= 0.0 || doubleArgs[0] >= 1.0)
	  return StrategicExecution::DIE;
	sampledInteger = geometric_distribution<long int>(doubleArgs[0])(choice_random_generator);
	break;
      case POISSON:
	if (doubleArgs[0] <= 0.0)
	  return StrategicExecution::DIE;
	sampledInteger = poisson_distribution<long int>(doubleArgs[0])(choice_random_generator);
	break;
      case CAUCHY:
        if (doubleArgs[1] <= 0.0)
	  return StrategicExecution::DIE;
	sampled = cauchy_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case WEIBULL:
        if (doubleArgs[0] <= 0.0 || doubleArgs[1] <= 0.0)
	  return StrategicExecution::DIE;
	sampled = weibull_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case LOGNORMAL:
        if (doubleArgs[1] <= 0.0)
	  return StrategicExecution::DIE;
	sampled = lognormal_distribution<double>(doubleArgs[0], doubleArgs[1])(choice_random_generator);
	break;
      case CHI2:
	if (integerArgs[0] <= 0)
	  return StrategicExecution::DIE;
	sampled = chi_squared_distribution<double>(integerArgs[0])(choice_random_generator);
	break;
      case STUDENTT:
	if (integerArgs[0] <= 0)
	  return StrategicExecution::DIE;
	sampled = student_t_distribution<double>(integerArgs[0])(choice_random_generator);
	break;
      case NUM_DISTRIBUTIONS:
	CantHappen("bad distribution");
    }

  // Open a variable context where the sample variable is defined
  int index = static_cast<VariableTerm*>(variable)->getIndex();
  Substitution sb(index + 1);

  switch (signature.result)
    {
      case DistributionSignature::Type::BOOL:
        sb.bind(index, sampledInteger ? trueTerm.getDag() : falseTerm.getDag());
	break;
      case DistributionSignature::Type::FLOAT:
        sb.bind(index, new FloatDagNode(safeCast(FloatSymbol*, resultSymbol), sampled));
	break;
      default:
        // minusSymbol is not stored in the strategy object because the uniform
	// distribution with a negative index is the only distribution that may
	// generate negative integer, so we catch the symbol above
	if (sampledInteger < 0)
	  {
	    DagNode* result = minusSymbol->makeNegDag(sampledInteger);
	    result->computeBaseSortForGroundSubterms(false);
	    sb.bind(index, result);
	    // The sample variable could be a Nat variable, in which case we should fail
	    if (!leq(result->getSort(), variable->getSort()))
	      return StrategicExecution::DIE;
	  }
	else if (sampledInteger >= 0)
	  sb.bind(index, safeCast(SuccSymbol*, resultSymbol)->makeNatDag64(sampledInteger));
    }

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
