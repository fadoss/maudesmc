/*

    This file is part of the Maude 2 interpreter.

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
//      Class for symbols for built-in meta-level operations.
//
#ifndef _cetaSymbol_hh_
#define _cetaSymbol_hh_

//      utility stuff
#include "macros.hh"
#include "vector.hh"

// forward declarations
#include "core.hh"
#include "interface.hh"
#include "freeTheory.hh"
#include "higher.hh"
#include "variable.hh"
#include "mixfix.hh"
#include "strategyLanguage.hh"
#include "SMT.hh"

//      free theory class definitions
#include "freeSymbol.hh"

#include "meta.hh"
#include "ACU_Theory.hh"

class CetaSymbol : public FreeSymbol
{
  NO_COPYING(CetaSymbol);

public:
  CetaSymbol(int id, int nrArgs);
  ~CetaSymbol();

  bool attachData(const Vector<Sort*>& opDeclaration,
		  const char* purpose,
		  const Vector<const char*>& data);
  bool attachSymbol(const char* purpose, Symbol* symbol);
  bool attachTerm(const char* purpose, Term* term);
  void copyAttachments(Symbol* original, SymbolMap* map);
  void getSymbolAttachments(Vector<const char*>& purposes,
			    Vector<Symbol*>& symbols);
  void getTermAttachments(Vector<const char*>& purposes,
			  Vector<Term*>& terms);

  void postInterSymbolPass();
  void compileEquations();
  void reset();
private:
  bool okToBind();
  bool setReachability(FreeDagNode* subject, RewritingContext& context);

  bool eqRewrite(DagNode* subject, RewritingContext& context);
  static bool eqRewrite(Symbol* symbol, DagNode* subject, RewritingContext& context);

  MetaLevel* metaLevel;
  MetaLevelOpSymbol* shareWith;
  Symbol* emptySymbol;
  Symbol* acceptingTermSymbol;
  Symbol* invalidInputSymbol;
  ACU_Symbol* qidSetSymbol;
  Term* trueTerm;
};

#endif
