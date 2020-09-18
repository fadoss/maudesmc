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
//      Class for symbols for built in model checking.
//
#ifndef _specialHubSymbol_hh_
#define _specialHubSymbol_hh_
#include "freeSymbol.hh"
#include "cachedDag.hh"

#include <map>
#include <vector>

class SpecialHubSymbol : public FreeSymbol
{
  NO_COPYING(SpecialHubSymbol);

public:
  SpecialHubSymbol(int id, int arity);

  bool attachData(const Vector<Sort*>& opDeclaration,
		  const char* purpose,
		  const Vector<const char*>& data);
  bool attachSymbol(const char* purpose, Symbol* symbol);
  bool attachTerm(const char* purpose, Term* term);
  void copyAttachments(Symbol* original, SymbolMap* map);
  void getDataAttachments(const Vector<Sort*>& opDeclaration,
			  Vector<const char*>& purposes,
			  Vector<Vector<const char*> >& data);
  void getSymbolAttachments(Vector<const char*>& purposes,
			    Vector<Symbol*>& symbols);
  void getTermAttachments(Vector<const char*>& purposes,
			  Vector<Term*>& terms);

  bool eqRewrite(DagNode* subject, RewritingContext& context);
  DagNode* ruleRewrite(DagNode* subject, RewritingContext& context);
  void postInterSymbolPass();
  void reset();

  typedef map<string, Symbol*> SymbolHooks;
  typedef map<string, CachedDag> TermHooks;

  // Signature of the callback function invoked when an equational
  // or rule rewrite takes place place in the special symbol. The
  // current term and the information attached to the symbol are
  // passed. A null return value indicates no rewite.
  typedef DagNode* (*SpecialCallback)(DagNode*,
				      const vector<string> &,
				      const SymbolHooks&,
				      TermHooks&,
				      void*);

  // Connect a callback to a special operator identifier, either for
  // equational or rule rewriting. The third argument is an opaque
  // pointer to be passed to the callback function. The identifier is
  // the operator name, or the first argument of the id-hook if any.
  static bool connectReduce(const char* name, SpecialCallback cb, void* data);
  static bool connectRewrite(const char* name, SpecialCallback cb, void* data);

private:
  //
  //	Data, symbols and terms attached to the symbol
  //
  vector<string> symbolData;
  SymbolHooks symbols;
  TermHooks terms;

  // Name to register callbacks
  const char* name;

  //
  // Static tables from special operator names to their handlers
  // (including the callback and an opaque pointer)
  //
  typedef pair<SpecialCallback, void*> CallbackData;
  typedef map<string, CallbackData> CallbackMap;

  static CallbackMap eqCallbacks;
  static CallbackMap rlCallbacks;
  static CallbackData defaultEqCallback;
  static CallbackData defaultRlCallback;

  static const CallbackData& getCallback(const char* name,
					 const CallbackMap &map,
					 const CallbackData &defval);
};

#endif
