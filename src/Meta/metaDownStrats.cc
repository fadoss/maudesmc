/*

    This file is part of the Maude 3 interpreter.

    Copyright 1997-2003 SRI International, Menlo Park, CA 94025, USA.

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
//	Code to move strategy expressions and declarations down from meta-level to object level.
//

StrategyExpression*
MetaLevel::downStratExpr(DagNode* metaStrategy,
			 MixfixModule* m)
{
  Symbol* mc = metaStrategy->symbol();

  if (mc == idleStratSymbol)
    return new TrivialStrategy(true);

  else if (mc == failStratSymbol)
    return new TrivialStrategy(false);

  else if (mc == allStratSymbol)
    {
      Vector<Term*> variables;
      Vector<Term*> values;
      Vector<StrategyExpression*> strategies;
      return new ApplicationStrategy(UNDEFINED, variables, values, strategies);
    }

  else if (mc == applicationStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      int id;
      Vector<Term*> variables, values;
      Vector<StrategyExpression*> strats;

      if (downQid(f->getArgument(0), id) &&
	  downSubstitution(f->getArgument(1), m, variables, values) &&
	  downStrategyList(f->getArgument(2), m, strats))
	return new ApplicationStrategy(id, variables, values, strats);

      // In case of error, frees the already constructed components
      for (int i = variables.length() - 1; i >= 0; i--)
	{
	  variables[i]->deepSelfDestruct();
	  values[i]->deepSelfDestruct();
	}
    }
  else if (mc == topStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (StrategyExpression* child = downStratExpr(f->getArgument(0), m))
	{
	  ApplicationStrategy* appStrat = static_cast<ApplicationStrategy*>(child);
	  Assert(appStrat, "strategy below top is not application");
	  appStrat->setTop();
	  return appStrat;
	}
    }
  else if (mc == oneStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (StrategyExpression* child = downStratExpr(f->getArgument(0), m))
	return new OneStrategy(child);
    }
  else if (mc == matchStratSymbol || mc == xmatchStratSymbol || mc == amatchStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      int depth = mc == matchStratSymbol ? -1 : (mc == xmatchStratSymbol ? 0 : UNBOUNDED);

      if (Term* pattern = downTerm(f->getArgument(0), m))
	{
	  Vector<ConditionFragment*> condition;
	  if (downCondition(f->getArgument(1), m, condition))
	    return new TestStrategy(pattern, depth, condition);
	  else
	    pattern->deepSelfDestruct();
	}
    }
  else if (mc == unionStratSymbol)
    {
      Vector<StrategyExpression*> strats;

      for (DagArgumentIterator i(metaStrategy); i.valid(); i.next())
	{
	  if (StrategyExpression* child = downStratExpr(i.argument(), m))
	    strats.append(child);
	  else
	    {
	      for (StrategyExpression* s : strats)
		delete s;
	      return 0;
	    }
	}

      return new UnionStrategy(strats);
    }
  else if (mc == concatStratSymbol)
    {
      Vector<StrategyExpression*> strats;

      for (DagArgumentIterator i(metaStrategy); i.valid(); i.next())
	{
	  if (StrategyExpression* child = downStratExpr(i.argument(), m))
	    strats.append(child);
	  else
	    {
	      for (StrategyExpression* s : strats)
		delete s;
	      return 0;
	    }
	}

      return new ConcatenationStrategy(strats);
    }
  else if (mc == orelseStratSymbol)
    {
      Vector<StrategyExpression*> strats;

      for (DagArgumentIterator i(metaStrategy); i.valid(); i.next())
	{
	  if (StrategyExpression* child = downStratExpr(i.argument(), m))
	    strats.append(child);
	  else
	    {
	      for (StrategyExpression* s : strats)
		delete s;
	      return 0;
	    }
	}

      // Orelse is right associative, we have to build a BranchStrategy
      // tree accordingly
      int nrStrats = strats.size();
      StrategyExpression* rest = strats[nrStrats - 1];

      for (int i = nrStrats - 2; i >= 0; i--)
	rest = new BranchStrategy(strats[i],
				  BranchStrategy::PASS_THRU,
				  0,
				  BranchStrategy::NEW_STRATEGY,
				  rest);

      return rest;
    }
  else if (mc == plusStratSymbol || mc == starStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (StrategyExpression* child = downStratExpr(f->getArgument(0), m))
	return new IterationStrategy(child, mc == starStratSymbol);
    }
  else if (mc == conditionalStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (StrategyExpression* condition = downStratExpr(f->getArgument(0), m))
	{
	  if (StrategyExpression* trueBranch = downStratExpr(f->getArgument(1), m))
	    {
	      if (StrategyExpression* falseBranch = downStratExpr(f->getArgument(2), m))
		return new BranchStrategy(condition,
					  BranchStrategy::NEW_STRATEGY,
					  trueBranch,
					  BranchStrategy::NEW_STRATEGY,
					  falseBranch);
	      delete trueBranch;
	    }
	  delete condition;
	}
    }
  else if (mc == notStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (StrategyExpression* child = downStratExpr(f->getArgument(0), m))
	return new BranchStrategy(child,
				  BranchStrategy::FAIL,
				  0,
				  BranchStrategy::IDLE,
				  0);
    }
  else if (mc == testStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (StrategyExpression* child = downStratExpr(f->getArgument(0), m))
	return new BranchStrategy(child,
				  BranchStrategy::IDLE,
				  0,
				  BranchStrategy::FAIL,
				  0);
    }
  else if (mc == tryStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (StrategyExpression* child = downStratExpr(f->getArgument(0), m))
	return new BranchStrategy(child,
				  BranchStrategy::PASS_THRU,
				  0,
				  BranchStrategy::IDLE,
				  0);
    }
  else if (mc == normalizationStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (StrategyExpression* child = downStratExpr(f->getArgument(0), m))
	return new BranchStrategy(child,
				  BranchStrategy::ITERATE,
				  0,
				  BranchStrategy::IDLE,
				  0);
    }
  else if (mc == matchrewStratSymbol ||
	   mc == xmatchrewStratSymbol ||
	   mc == amatchrewStratSymbol ||
	   mc == matchrewWeightStratSymbol ||
	   mc == xmatchrewWeightStratSymbol ||
	   mc == amatchrewWeightStratSymbol)
    {
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      Vector<Term*> subpatterns;
      Vector<StrategyExpression*> substrats;

      bool weighted = mc == matchrewWeightStratSymbol ||
		      mc == xmatchrewWeightStratSymbol ||
		      mc == amatchrewWeightStratSymbol;

      Term* pattern;
      if (downTermStrategyList(f->getArgument(weighted ? 3 : 2), m, subpatterns, substrats)
	  && (pattern = downTerm(f->getArgument(0), m)))
	{
	  int depth = (mc == matchrewStratSymbol || mc == matchrewWeightStratSymbol) ? -1 :
		      ((mc == xmatchrewStratSymbol || mc == xmatchrewWeightStratSymbol) ? 0 :
		       UNBOUNDED);

	  Vector<ConditionFragment*> condition;
	  if (downCondition(f->getArgument(1), m, condition))
	    if (weighted)
	      {
		if (Term* weight = downTerm(f->getArgument(2), m))
		  return new WeightedSubtermStrategy(pattern, depth, condition,
						     subpatterns, substrats, weight);
	      }
	     else
	       return new SubtermStrategy(pattern, depth, condition,
					 subpatterns, substrats);
	  else
	    pattern->deepSelfDestruct();
	}
    }
  else if (mc == callStratSymbol)
    {
      RewriteStrategy* strat;
      Term* callTerm;

      if (downStratCall(metaStrategy, m, strat, callTerm))
	return new CallStrategy(strat, callTerm);
    }
  else if (mc == choiceStratSymbol)
    {
      Vector<Term*> weights;
      Vector<StrategyExpression*> strats;
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (downTermStrategyList(f->getArgument(0), m, weights, strats))
	return new ChoiceStrategy(weights, strats);
    }
  else if (mc == sampleStratSymbol)
    {
      SampleStrategy::Distribution distrib = SampleStrategy::NUM_DISTRIBUTIONS;
      Vector<Term*> args;
      FreeDagNode* f = static_cast<FreeDagNode*>(metaStrategy);

      if (f->getArgument(1)->symbol() == metaTermSymbol)
	{
	  FreeDagNode* d = static_cast<FreeDagNode*>(f->getArgument(1));
	  int id;

	  if (downQid(d->getArgument(0), id) && downTermList(d->getArgument(1), m, args))
	    {
	      const char* name = Token::name(id);

	      // Find the distribution by its name (not efficient, but there are few)
	      for (int i = 0; i < SampleStrategy::NUM_DISTRIBUTIONS; i++)
		{
		  SampleStrategy::Distribution cd = (SampleStrategy::Distribution) i;

		  if (strcmp(name, SampleStrategy::getName(cd)) == 0)
		    distrib = cd;
		}

	      if (distrib != SampleStrategy::NUM_DISTRIBUTIONS)
		{
		  if (Term* variable = downTerm(f->getArgument(0), m))
		    {
		      if (StrategyExpression* strategy = downStratExpr(f->getArgument(2), m))
			  return new SampleStrategy(variable, distrib, args, strategy);
		      variable->deepSelfDestruct();
		    }
		}
	      else
		IssueWarning("bad probabilistic distribution in " << QUOTE(f->getArgument(1)) << ".");
	      // Destruct the argument terms in case of error
	      for (Term* arg : args)
		arg->deepSelfDestruct();
	    }
	}
    }

  return 0;
}

bool
MetaLevel::downTermStrategyList(DagNode* metaList,
				MixfixModule* m,
				Vector<Term*>& terms,
				Vector<StrategyExpression*>& strats)
{
  bool ok = true;
  FreeDagNode* pair = dynamic_cast<FreeDagNode*>(metaList);

  if (pair != 0)	// it is a single pair
    {
      if (Term* term = downTerm(pair->getArgument(0), m))
	{
	  terms.append(term);
	  if (StrategyExpression* strat = downStratExpr(pair->getArgument(1), m))
	      strats.append(strat);
	  else
	    ok = false;
	}
      else
        ok = false;
    }
  else
    {
      for (DagArgumentIterator i(metaList); ok && i.valid(); i.next())
	{
	  pair = static_cast<FreeDagNode*>(i.argument());
	  if (Term* term = downTerm(pair->getArgument(0), m))
	    {
	      terms.append(term);
	      if (StrategyExpression* strat = downStratExpr(pair->getArgument(1), m))
		strats.append(strat);
	      else
		ok = false;
	    }
	  else
	    ok = false;
	}
    }

  // In case something has gone wrong, the already constructed
  // components are freed
  if (!ok)
    {
      for (Term* t : terms)
	t->deepSelfDestruct();
      for (StrategyExpression* s : strats)
	delete s;
    }

  return ok;
}

bool
MetaLevel::downStratDef(DagNode* metaStratDef, MixfixModule* m)
{
  Symbol* mse = metaStratDef->symbol();

  if (mse == sdSymbol || mse == csdSymbol)
    {
      FreeDagNode* f = safeCast(FreeDagNode*, metaStratDef);
      StatementAttributeInfo ai;
      if (downStatementAttrSet(f->getArgument((mse == sdSymbol) ? 2 : 3), m, ai))
	{
	  RewriteStrategy* strat = 0;
	  Term* callTerm = 0;

	  StrategyExpression* r = downStratExpr(f->getArgument(1), m);

	  if (r && downStratCall(f->getArgument(0), m, strat, callTerm))
	    {
	      Vector<ConditionFragment*> condition;
	      if (mse == sdSymbol || downCondition(f->getArgument(2), m, condition))
		{
		  StrategyDefinition* sdef = new StrategyDefinition(ai.label, strat, callTerm, r, condition);
		  if (ai.flags.getFlag(NONEXEC))
		    sdef->setNonexec();
		  m->insertStrategyDefinition(sdef);
		  if (ai.metadata != NONE)
		    m->insertMetadata(MixfixModule::STRAT_DEF, sdef, ai.metadata);
		  if (ai.flags.getFlag(PRINT))
		    m->insertPrintAttribute(MixfixModule::STRAT_DEF, sdef, ai.printNames, ai.printSorts);
		  return true;
		}
	    }

	  if (callTerm) callTerm->deepSelfDestruct();
	  delete r;
	}
    }
  return false;
}

bool
MetaLevel::downStrategyList(DagNode* metaList, MixfixModule* m, Vector<StrategyExpression*>& list)
{
  list.clear();
  Symbol* mtl = metaList->symbol();

  if (mtl == stratListSymbol)
    {
      for (DagArgumentIterator i(metaList); i.valid(); i.next())
	{
	  StrategyExpression* e = downStratExpr(i.argument(), m);
	  if (!e)
	    {
	      for (StrategyExpression* s : list)
		delete s;
	      return false;
	    }
	  list.append(e);
	}
    }
  else if (mtl == emptyStratListSymbol)
    return true;

  else
    {
      StrategyExpression* e = downStratExpr(metaList, m);
      if (!e)
	return false;

      list.append(e);
    }

  return true;
}

bool
MetaLevel::downStratDecl(DagNode* metaStratDecl, MixfixModule* m)
{
  Assert(metaStratDecl->symbol() == stratDeclSymbol, "bad meta strategy declaration symbol");

  FreeDagNode* f = safeCast(FreeDagNode*, metaStratDecl);

  int label;
  AttributeInfo ai;	// Not allowed attributed are silently ignored
  Vector<Sort*> domainTypes;
  Sort* subjectSort;

  if (downQid(f->getArgument(0), label) &&
      downAttrSet(f->getArgument(3), ai) &&
      downTypeList(f->getArgument(1), m, domainTypes) &&
      downType(f->getArgument(2), m, subjectSort))
    {
      Token name;
      name.tokenize(label, FileTable::META_LEVEL_CREATED);
      m->addStrategy(name, domainTypes, subjectSort, ai.metadata);
      return true;
    }

  return false;
}

bool
MetaLevel::downStratDecls(DagNode* metaStratDecls, MixfixModule* m)
{
  Symbol* ms = metaStratDecls->symbol();

  if (ms == stratDeclSetSymbol)
    {
      for (DagArgumentIterator i(metaStratDecls); i.valid(); i.next())
	if (!downStratDecl(i.argument(), m))
	  return false;
    }
  else if (ms != emptyStratDeclSetSymbol)
    return downStratDecl(metaStratDecls, m);

  return true;
}

bool
MetaLevel::downStratDefs(DagNode* metaStratDefs, MixfixModule* m)
{
  Symbol* ms = metaStratDefs->symbol();

  if (ms == stratDefSetSymbol)
    {
      for (DagArgumentIterator i(metaStratDefs); i.valid(); i.next())
	if (!downStratDef(i.argument(), m))
	  return false;
    }
  else if (ms != emptyStratDefSetSymbol)
    return downStratDef(metaStratDefs, m);

  return true;
}

bool
MetaLevel::downStratCall(DagNode* metaStratCall, MixfixModule* m, RewriteStrategy*& strat, Term*& callTerm)
{
  Assert(metaStratCall->symbol() == callStratSymbol, "bad strategy call symbol");

  FreeDagNode* f = safeCast(FreeDagNode*, metaStratCall);

  int label;
  Vector<Term*> args;

  if (downQid(f->getArgument(0), label) && downTermList(f->getArgument(1), m, args))
    {
      int nrArgs = args.size();
      Vector<ConnectedComponent*> domainComponents(nrArgs);
      for (int i = 0; i < nrArgs; i++)
	domainComponents[i] = args[i]->symbol()->rangeComponent();

      strat = m->findStrategy(label, domainComponents);

      if (strat)
	{
	  callTerm = strat->makeAuxiliaryTerm(args);
	  return true;
	}

      IssueAdvisory("could not find an strategy " << QUOTE(Token::name(label)) <<
		    " with appropriate domain in meta-module " << QUOTE(m) <<
		    " when trying to interprete meta strategy call " <<
		    QUOTE(metaStratCall) << '.');

      for (Term* t : args)
	t->deepSelfDestruct();
    }

  return false;
}
