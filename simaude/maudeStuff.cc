/**
 * @file maudeStuff.cc
 *
 * Direct interaction with Maude.
 */

// Maude library headers
#include "macros.hh"
#include "vector.hh"

// forward declarations
#include "interface.hh"
#include "core.hh"
#include "higher.hh"
#include "freeTheory.hh"
#include "builtIn.hh"
#include "strategyLanguage.hh"
#include "mixfix.hh"

// required classes
#include "interpreter.hh"
#include "token.hh"
#include "strategyLanguage.hh"
#include "strategyExpression.hh"
#include "global.hh"
#include "syntacticPreModule.hh"
#include "mixfixParser.hh"
#include "visibleModule.hh"
#include "lexerAux.hh"
#include "directoryManager.hh"
#include "userLevelRewritingContext.hh"
#include "stateTransitionGraph.hh"
#include "strategyTransitionGraph.hh"
#include "cachedDag.hh"
#include "meta.hh"
#include "metaLevel.hh"
#include "metaModule.hh"
#include "metaLevelOpSymbol.hh"
#include "fairStrategicSearch.hh"

// To retrieve the module path (dladdr, non-standard)
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#else
#include <link.h>
#endif

#include "maudeStuff.hh"

bool
loadMaudeFile(const char* filename, bool readPrelude) {
	// Exported from Maude
	bool includeFile(const string& directory, const string& fileName, bool silent, int lineNr);
	void createRootBuffer(FILE* fp, bool forceInteractive);
	int yyparse(UserLevelRewritingContext::ParseResult*);


	FILE* fp = fopen(filename, "r");

	if (fp == nullptr) {
		perror(filename);
		return false;
	}

	createRootBuffer(fp, false);

	// Disable advising
	globalAdvisoryFlag = false;

	// Obtain the path where the Maude library is to locate the prelude
	#ifdef _WIN32
	char buffer[FILENAME_MAX];
	GetModuleFileName(GetModuleHandle("libmaude.dll"), buffer, FILENAME_MAX);
	string executable(buffer);
	#else
	Dl_info dlinfo;
	dladdr((void*) &createRootBuffer, &dlinfo);
	string executable(dlinfo.dli_fname);
	#endif

	directoryManager.initialize();
	findExecutableDirectory(executableDirectory, executable);
	if (readPrelude) {
		string directory;
		string fileName(PRELUDE_NAME);
		if (findPrelude(directory, fileName))
			includeFile(directory, fileName, true, FileTable::AUTOMATIC);
		else {
			cerr << "cannot find Maude prelude (setting MAUDE_LIB environment variable could help)\n";
			fclose(fp);
			return false;
		}
	}
	else
		checkForPending();  // because we won't hit an EOF

	ioManager.startCommand();
	UserLevelRewritingContext::ParseResult parseResult = UserLevelRewritingContext::NORMAL;
	while (parseResult == UserLevelRewritingContext::NORMAL) {
		bool parseError = yyparse(&parseResult);

		if (parseError) {
			cerr << "there were errors when processing the input file\n";
			fclose(fp);
			return false;
		}
	}

	return true;
}

VisibleModule*
selectModule(const char* modulename) {

	PreModule* premodule;

	// If a name was given, look for such module
	if (modulename != nullptr && *modulename != '\0') {
		premodule = interpreter.getModule(Token::encode(modulename));

		if (premodule == nullptr) {
			cerr << "module " << modulename << " does not exist\n";
			return nullptr;
		}

	}
	// Get the current module (the last one if not explicitly selected
	// using the select command)
	else {
		premodule = interpreter.getCurrentModule();
		// Printf(infoShort, "%s: selected module is %s\n", pins_plugin_name, Token::name(premodule->id()));
	}

	// Abort if the module has syntactic problems
	if (premodule->getFlatSignature()->isBad()) {
		cerr << "module " << Token::name(premodule->id()) << " is unusable\n";
		return nullptr;
	}

	// Import the module statements and finish it
	VisibleModule* mod = premodule->getFlatModule();

	if (mod->isBad()) {
		cerr << "module " << Token::name(premodule->id()) << " is unusable\n";
		return nullptr;
	}

	return mod;
}

VisibleModule*
selectMetamodule(VisibleModule* mod, const char* moduleTerm) {

	// Parse and reduce the metamodule in the current module
	Term* term = parseTerm(mod, moduleTerm);

	if (term == nullptr) {
		cerr << "cannot parse meta-module term\n";
		return nullptr;
	}

	DagNode* dagMetamod = toDag(term);
	RewritingContext* context = new UserLevelRewritingContext(dagMetamod);
	context->reduce();

	// Turn the metamodule into a object-level module
	// A valid instance of the MetaLevel class is obtained through any
	// special operator of type MetaLevelOpSymbol in the module where the
	// term is parsed, which must include META-LEVEL

	// Finds an operator of type MetaLevelOpSymbol for which to obtain
	// the MetaLevel instance

	const Vector<Symbol*> &symbols = mod->getSymbols();
	int symbolIndex = mod->getNrUserSymbols() - 1;

	MetaLevelOpSymbol* metaSymbol = nullptr;

	while (metaSymbol == nullptr && symbolIndex >= 0)
		metaSymbol = dynamic_cast<MetaLevelOpSymbol*>(symbols[symbolIndex--]);

	if (metaSymbol == nullptr) {
		cerr << "cannot get access to the metalevel "
		        "(META-LEVEL must be included in the module "
		        "where the metamodule is expressed)\n";
		return nullptr;
	}

	VisibleModule* metamodule = metaSymbol->getMetaLevel()->downModule(dagMetamod);

	if (metamodule == nullptr) {
		cerr << "not a valid meta-module\n";
		return nullptr;
	}

	// The meta-module term is no longer needed
	delete context;

	return metamodule;
}

void
tokenizeString(const char* str, Vector<Token> &tokens) {
	// Exported from Maude
	const Vector<int>* tokenizeRope(const Rope& argumentRope);

	Rope rope(str);
	const Vector<int>* tokenCodes = tokenizeRope(rope);

	size_t nrTokens = tokenCodes->size();
	tokens.resize(nrTokens);
	for (size_t i = 0; i < nrTokens; i++)
		tokens[i].tokenize((*tokenCodes)[i], 0);
}

DagNode*
toDag(Term* term) {
	NatSet emptySet;
	Vector<int> emptyVector;
	bool changed;

	term->markEagerArguments(0, emptySet, emptyVector);
	term->normalize(true, changed);
	DagNode* dag = term->term2Dag(term->getSortIndex() != Sort::SORT_UNKNOWN);

	// The term is freed after the conversion
	term->deepSelfDestruct();
	return dag;
}

Term*
parseTerm(VisibleModule* mod, const char* txt) {
	Vector<Token> tokens;
	tokenizeString(txt, tokens);

	return mod->parseTerm(tokens);
}

StrategyExpression*
parseStrategyExpr(VisibleModule* mod, const char* txt) {
	Vector<Token> tokens;
	tokenizeString(txt, tokens);

	// Parse the given strategy expression
	StrategyExpression* strategy = mod->parseStrategyExpr(tokens);

	if (strategy == nullptr) {
		cerr << "cannot parse strategy" << endl;
		return nullptr;
	}

	VariableInfo vinfo;
	TermSet termSet;

	// Check whether the strategy expression is well-formed and prepare it
	if (!strategy->check(vinfo, termSet)) {
		cout << "bad strategy" << endl;
		return nullptr;
	}
	strategy->process();

	return strategy;
}
