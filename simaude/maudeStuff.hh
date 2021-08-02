/**
 * @file maudeStuff.hh
 *
 * Direct interaction with Maude.
 */
 
#ifndef _maudeStuff_hh_
#define _maudeStuff_hh_

#include "mixfix.hh"
#include "interface.hh"
#include "strategyLanguage.hh"

bool loadMaudeFile(const char* filename, bool readPrelude);
VisibleModule* selectModule(const char* modulename);
VisibleModule* selectMetamodule(VisibleModule* mod, const char* moduleTerm);
Term* parseTerm(VisibleModule* mod, const char* txt);
StrategyExpression* parseStrategyExpr(VisibleModule* mod, const char* txt);
DagNode* toDag(Term* term);

#endif
