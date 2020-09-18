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
//      Implementation for class SpecialHubSymbol.
//

//      utility stuff
#include "macros.hh"
#include "vector.hh"

//      forward declarations
#include "interface.hh"
#include "core.hh"
#include "freeTheory.hh"
#include "builtIn.hh"
#include "higher.hh"

//      interface class definitions
#include "symbol.hh"
#include "dagNode.hh"
#include "term.hh"

//      core class definitions
#include "dagArgumentIterator.hh"
#include "rewritingContext.hh"
#include "symbolMap.hh"

//      free theory class definitions
#include "freeDagNode.hh"

//      built in class definitions
#include "bindingMacros.hh"

//HACK
#include "token.hh"

//      higher class definitions
#include "specialHubSymbol.hh"

#include <cstring> // TEMPORAL (para evitar quejas)

// Static variables
SpecialHubSymbol::CallbackMap SpecialHubSymbol::eqCallbacks;
SpecialHubSymbol::CallbackMap SpecialHubSymbol::rlCallbacks;
SpecialHubSymbol::CallbackData SpecialHubSymbol::defaultEqCallback = {nullptr, nullptr};
SpecialHubSymbol::CallbackData SpecialHubSymbol::defaultRlCallback = {nullptr, nullptr};

inline
const SpecialHubSymbol::CallbackData&
SpecialHubSymbol::getCallback(const char* name,
			      const SpecialHubSymbol::CallbackMap &map,
			      const SpecialHubSymbol::CallbackData &defval)
{
  auto it = map.find(name);

  if (it != map.end())
    return it->second;

  return defval;
}

SpecialHubSymbol::SpecialHubSymbol(int id, int arity)
  : FreeSymbol(id, arity), name(Token::name(id))
{
}

bool
SpecialHubSymbol::attachData(const Vector<Sort*>& opDeclaration,
			     const char* purpose,
			     const Vector<const char*>& data)
{
  if (strcmp(purpose, "SpecialHubSymbol") == 0)
    {
      symbolData.clear();
      symbolData.insert(symbolData.end(), data.begin(), data.end());
      // Replace the name by the first data argument (if any)
      if (!data.empty())
	name = symbolData[0].c_str();
      return true;
    }
  return  FreeSymbol::attachData(opDeclaration, purpose, data);
}

bool
SpecialHubSymbol::attachSymbol(const char* purpose, Symbol* symbol)
{
  symbols.emplace(purpose, symbol);
  return true;
}

bool
SpecialHubSymbol::attachTerm(const char* purpose, Term* term)
{
  bool r = true;
  auto it = terms.find(purpose);

  if (it != terms.end())
    {
      r = term->equal(it->second.getTerm());
      term->deepSelfDestruct();
    }
  else
    terms.emplace(purpose, term);

  return r;
}

void
SpecialHubSymbol::copyAttachments(Symbol* original, SymbolMap* map)
{
  SpecialHubSymbol* orig = safeCast(SpecialHubSymbol*, original);
  name = orig->name;

  // Copy symbols
  for (const auto &entry : orig->symbols) {
    auto it = symbols.find(entry.first);

    if (it == symbols.end()) {
      symbols[entry.first] = (map == 0) ? entry.second
	: map->translate(entry.second);
    }
  }

  // Copy terms
  for (const auto &entry : orig->terms) {
    auto it = terms.find(entry.first);

    if (it == terms.end())
      terms[entry.first].setTerm(entry.second.getTerm()->deepCopy(map));
  }

  FreeSymbol::copyAttachments(original, map);
}

void
SpecialHubSymbol::getDataAttachments(const Vector<Sort*>& opDeclaration,
				     Vector<const char*>& purposes,
				     Vector<Vector<const char*> >& data)
{
  APPEND_DATA(purposes, data, SpecialHubSymbol);
  FreeSymbol::getDataAttachments(opDeclaration, purposes, data);
}

void
SpecialHubSymbol::getSymbolAttachments(Vector<const char*>& purposes,
				       Vector<Symbol*>& symbols)
{
  for (auto entry : this->symbols)
    {
      purposes.append(entry.first.c_str());
      symbols.append(entry.second);
    }
  FreeSymbol::getSymbolAttachments(purposes, symbols);
}

void
SpecialHubSymbol::getTermAttachments(Vector<const char*>& purposes,
				     Vector<Term*>& terms)
{
  for (const auto &entry : this->terms)
    {
      purposes.append(entry.first.c_str());
      terms.append(entry.second.getTerm());
    }
  FreeSymbol::getTermAttachments(purposes, terms);
}

void
SpecialHubSymbol::postInterSymbolPass()
{
  for (auto &entry : terms)
    PREPARE_TERM(entry.second);
  FreeSymbol::postInterSymbolPass();
}

void
SpecialHubSymbol::reset()
{
  for (auto &entry : terms)
    entry.second.reset();  // so these DAG nodes can be garbage collected
  FreeSymbol::reset();  // parents reset() tasks
}

bool
SpecialHubSymbol::eqRewrite(DagNode* subject, RewritingContext& context)
{
  Assert(this == subject->symbol(), "bad symbol");

  //
  // Dispatch execution to the connected handler
  //
  CallbackData cbdata = getCallback(name, eqCallbacks, defaultEqCallback);

  // If no callback is registered, the term is reduced as usual
  if (cbdata.first == nullptr)
    return FreeSymbol::eqRewrite(subject, context);

  // Call the connected callback, passing on the hook's and its own data
  DagNode* resultDag = (*cbdata.first)(subject, symbolData, symbols, terms, cbdata.second);

  // Null return values mean no rewrite possible
  if (resultDag == nullptr)
    return false;

  return context.builtInReplace(subject, resultDag);
}

DagNode*
SpecialHubSymbol::ruleRewrite(DagNode* subject, RewritingContext& context)
{
  Assert(this == subject->symbol(), "bad symbol");

  //
  // Dispatch execution to connected handler
  //
  CallbackData cbdata = getCallback(name, rlCallbacks, defaultRlCallback);

  if (cbdata.first != nullptr)
    {
      DagNode* result = (*cbdata.first)(subject, symbolData, symbols, terms, cbdata.second);

      if (result != nullptr)
	return result;
    }

  return FreeSymbol::ruleRewrite(subject, context);
}

bool
SpecialHubSymbol::connectReduce(const char* name, SpecialCallback cb, void* data)
{
  if (name == nullptr) {
    bool alreadyDefined = defaultEqCallback.first != nullptr;
    defaultEqCallback = {cb, data};
    return alreadyDefined;
  }

  auto it = eqCallbacks.find(name);
  bool contains = it != eqCallbacks.end();

  if (cb == nullptr)
    {
      if (contains)
	eqCallbacks.erase(it);
    }
  else
    eqCallbacks[name] = {cb, data};

  return contains;
}

bool
SpecialHubSymbol::connectRewrite(const char* name, SpecialCallback cb, void* data)
{
  if (name == nullptr) {
    bool alreadyDefined = defaultRlCallback.first != nullptr;
    defaultRlCallback = {cb, data};
    return alreadyDefined;
  }

  auto it = rlCallbacks.find(name);
  bool contains = it != rlCallbacks.end();

  if (cb == nullptr)
    {
      if (contains)
	rlCallbacks.erase(it);
    }
  else
    rlCallbacks[name] = {cb, data};

  return contains;
}
