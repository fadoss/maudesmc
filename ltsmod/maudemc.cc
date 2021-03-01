/**
 * @file maudemc.cc
 *
 * Implementation of a Maude language module for LTSmin.
 */

// By default, the system state is represented by an integer variable, but
// it is possible to use string chunks instead in order to obtain meaningful
// counterexamples, at some cost in speed and memory.
//#define STATE_AS_CHUNK

// C++ standard libraries
#include <iostream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <mutex>

// To retrieve the module path (dladdr, non-standard)
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#else
#include <link.h>
#endif

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

// LTSmin stuff
extern "C" {
#include <ltsmin/pins.h>
#include <ltsmin/pins-util.h>
#include <ltsmin/dlopen-api.h>
#include <ltsmin/ltsmin-standard.h>
#include <ltsmin/lts-type.h>
}

// the command line parser used by LTSmin
#include <popt.h>

using namespace std;

//
//	PINS interface
//

extern "C" {
	// Next-state functions for each model variant (replicated for efficiency)
	int next_state(void* model, int group, int *src, TransitionCB callback, void *arg);
	int next_state_strat(void* model, int group, int *src, TransitionCB callback, void *arg);
	int next_state_strat_purged(void* model, int group, int *src, TransitionCB callback, void *arg);
	int next_state_strat_merged(void* model, int group, int *src, TransitionCB callback, void *arg);
	int next_state_strat_merged_edge(void* model, int group, int *src, TransitionCB callback, void *arg);
	// State-label (i.e. atomic-proposition) checking function
	int state_label(model_t m, int label, int* src);
	// Maude language module entry point
	void pins_maude_model_init(model_t m, const char* maudef);
}

// Function to be called at the program exit to show some statistics
void at_exit();

// The name of plugin
char pins_plugin_name[] = "maude-mc";

// Handle maude files with the pins_maude_model_init function
struct loader_record pins_loaders[] = {
	{"maude", pins_maude_model_init},
	{nullptr, nullptr}
};

// Input arguments from the command line
char *popt_initial, *popt_aprops, *popt_module, *popt_metamodule, *popt_strategy, *popt_opaques, *popt_merge;
int popt_purge, popt_biased, popt_noadvise;

struct poptOption pins_options[] = {
	{"initial", '\0', POPT_ARG_STRING, &popt_initial, 0, "Initial state", nullptr},
	{"aprops", '\0', POPT_ARG_STRING, &popt_aprops, 0, "Atomic propositions that may appear in the formula (comma separated list of Maude terms)", nullptr},
	{"module", '\0', POPT_ARG_STRING, &popt_module, 0, "Maude module to select (by default, the last one)", nullptr},
	{"metamodule", '\0', POPT_ARG_STRING, &popt_metamodule, 0, "Maude term that reduces to a meta-module where to model check (optional)", nullptr},
	{"strat", '\0', POPT_ARG_STRING, &popt_strategy, 0, "Strategy expression to control the system (optional)", nullptr},
	{"merge-states", '\0', POPT_ARG_STRING, &popt_merge, 0, "Avoid artificial branching due to strategies by merging states", "state|edge|none"},
	{"purge-fails", '\0', POPT_ARG_NONE, &popt_purge, 0, "Remove states where the strategy has failed from the model", nullptr},
	{"biased-matchrew", '\0', POPT_ARG_NONE, &popt_biased, 0, "Disable generating all possible interleavings of subterm executions in matchrew", nullptr},
	{"opaque-strats", '\0', POPT_ARG_STRING, &popt_opaques, 0, "Strategies to be considered opaque (comma separated list)", nullptr},
	{"no-advise", '\0', POPT_ARG_NONE, &popt_noadvise, 0, "Disable showing Maude debugging and advisory messages", nullptr},
	POPT_TABLEEND
};


//
//	Maude PINS module
//

class MaudePINSModule {
	// Rewriting context for the subject term
	RewritingContext* context = nullptr;
	// Selected module where model checking is applied
	VisibleModule* module = nullptr;
	// State-transition graph with or without strategies
	// (only one of them is not null)
	StateTransitionGraph* graph = nullptr;
	StrategyTransitionGraph* sgraph = nullptr;
	// Bool's true term and satisfies symbol to test atomic propositions
	CachedDag trueTerm;
	Symbol* satisfiesSymbol = nullptr;

	// Atomic proposition terms (as DAG nodes) and the names they
	// take as LTSmin labels
	vector<DagRoot> atomicProps;
	vector<string> atomicNames;

	#ifdef STATE_AS_CHUNK
	// Index of the highest state known by the translator plus one
	int stateCount = 1;
	int state_type;
	#endif

	// Edge labels (translation from Maude's label indices to
	// edge label indices in LTSmin)
	map<int, int> elabelTranslation;


	// Valid states (when purging failed states)
	vector<bool> validStates;

	// Merged states (when using this feature) and reverse map
	vector<set<int>> mergedStates;
	map<set<int>, int> mergedStTable;

	// Maude does not support concurrent reductions and pins2lts-sym
	// may check state labels in parallel
	std::mutex mutex;

	// Wrapped getNextState for Maude graphs
	int getNextState(int stateNr, int index);

	bool checkProposition(int stateNr, int propIndex);
	void loadMaudeFile(const char* fileName, bool readPrelude = true);
	VisibleModule* selectModule(const char* moduleName);
	VisibleModule* selectMetamodule(const char* moduleTerm);

	// Read atomic props from a comma-separated list
	void readAtomicProps(const char* apspec);
	// Read opaque strategy names from a comma-separated list
	void readOpaqueStrats(const char* opaqueList, set<int>& opaques);

	Term* parseTerm(const char* txt);
	StrategyExpression* parseStrategyExpr(const char* txt);

	// Set up the internal Maude transition graph
	void setUpGraph(Term* initial, StrategyExpression* strategy);

	// Explore the transition graph detecting failed states
	// (when using the purged failed states option)
	void expand();
	bool expand(int state);

	friend void at_exit();
	friend int next_state(void* model, int group, int *src, TransitionCB callback, void *arg);
	friend int next_state_strat(void* model, int group, int *src, TransitionCB callback, void *arg);
	friend int next_state_strat_purged(void* model, int group, int *src, TransitionCB callback, void *arg);
	friend int next_state_strat_merged(void* model, int group, int *src, TransitionCB callback, void *arg);
	friend int next_state_strat_merged_edge(void* model, int group, int *src, TransitionCB callback, void *arg);
	friend int state_label(model_t m, int label, int* src);
	friend void pins_maude_model_init(model_t m, const char* maudef);
};

// Current instance of the module and the model
MaudePINSModule maudem;

//
//	Auxiliary functions and methods
//

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

#if STATE_AS_CHUNK
inline
string dagToString(DagNode* dag) {
	ostringstream stream;
	stream << dag;
	return stream.str();
}
#endif

inline int
MaudePINSModule::getNextState(int stateNr, int index) {
	return sgraph != nullptr
		? sgraph->getNextState(stateNr, index)
		: graph->getNextState(stateNr, index);
}

bool
MaudePINSModule::checkProposition(int stateNr, int propositionIndex) {
	Vector<DagNode*> args(2);
	if (mergedStates.size() == 0)
		args[0] = (graph != nullptr) ? graph->getStateDag(stateNr) : sgraph->getStateDag(stateNr);
	else
		args[0] = sgraph->getStateDag(*mergedStates[stateNr].begin());
	args[1] = atomicProps[propositionIndex].getNode();

	mutex.lock();
	// This is a critical section since the Maude rewriting engine is not reentrant
	RewritingContext* testContext = context->makeSubcontext(satisfiesSymbol->makeDagNode(args));
	testContext->reduce();
	bool result = trueTerm.getDag()->equal(testContext->root());
	context->addInCount(*testContext);
	delete testContext;
	mutex.unlock();

	return result;
}

inline void
MaudePINSModule::loadMaudeFile(const char* filename, bool readPrelude) {
	// Exported from Maude
	bool includeFile(const string& directory, const string& fileName, bool silent, int lineNr);
	void createRootBuffer(FILE* fp, bool forceInteractive);
	int yyparse(UserLevelRewritingContext::ParseResult*);


	FILE* fp = fopen(filename, "r");

	if (fp == nullptr) {
		perror(filename);
		ltsmin_abort(1);
	}

	createRootBuffer(fp, false);

	// Disable advising if requested
	if (popt_noadvise)
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
			Printf(assertion, "%s: cannot find Maude prelude (setting MAUDE_LIB environment variable could help)\n",
			       pins_plugin_name);
			fclose(fp);
			ltsmin_abort(2);
		}
	}
	else
		checkForPending();  // because we won't hit an EOF

	ioManager.startCommand();
	UserLevelRewritingContext::ParseResult parseResult = UserLevelRewritingContext::NORMAL;
	while (parseResult == UserLevelRewritingContext::NORMAL) {
		bool parseError = yyparse(&parseResult);

		if (parseError) {
			Printf(assertion, "%s: there were errors when processing the input file\n", pins_plugin_name);
			fclose(fp);
			ltsmin_abort(2);
		}
	}
}

inline VisibleModule*
MaudePINSModule::selectModule(const char* modulename) {

	PreModule* premodule;

	// If a name was given, look for such module
	if (modulename != nullptr && *modulename != '\0') {
		premodule = interpreter.getModule(Token::encode(modulename));

		if (premodule == nullptr) {
			Printf(assertion, "%s: module %s does not exist\n", pins_plugin_name, modulename);
			ltsmin_abort(1);
		}

	}
	// Get the current module (the last one if not explicitly selected
	// using the select command)
	else {
		premodule = interpreter.getCurrentModule();
		Printf(infoShort, "%s: selected module is %s\n", pins_plugin_name, Token::name(premodule->id()));
	}

	// Abort if the module has syntactic problems
	if (premodule->getFlatSignature()->isBad()) {
		Printf(assertion, "%s: module %s is unusable\n", pins_plugin_name, Token::name(premodule->id()));
		ltsmin_abort(2);
	}

	// Import the module statements and finish it
	module = premodule->getFlatModule();

	if (module->isBad()) {
		Printf(assertion, "%s: module %s is unusable\n", pins_plugin_name, Token::name(module->id()));
		ltsmin_abort(2);
	}

	return module;
}

inline VisibleModule*
MaudePINSModule::selectMetamodule(const char* moduleTerm) {

	// Parse and reduce the metamodule in the current module
	Term* term = parseTerm(moduleTerm);

	if (term == nullptr) {
		Printf(assertion, "%s: cannot parse meta-module term\n", pins_plugin_name);
		ltsmin_abort(3);
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

	const Vector<Symbol*> &symbols = module->getSymbols();
	int symbolIndex = module->getNrUserSymbols() - 1;

	MetaLevelOpSymbol* metaSymbol = nullptr;

	while (metaSymbol == nullptr && symbolIndex >= 0)
		metaSymbol = dynamic_cast<MetaLevelOpSymbol*>(symbols[symbolIndex--]);

	if (metaSymbol == nullptr) {
		Printf(assertion, "%s: cannot get access to the metalevel "
		                  "(META-LEVEL must be included in the module "
		                  "where the metamodule is expressed)\n", pins_plugin_name);
		ltsmin_abort(3);
	}

	VisibleModule* metamodule = metaSymbol->getMetaLevel()->downModule(dagMetamod);

	if (metamodule == nullptr) {
		Printf(assertion, "%s: not a valid meta-module\n", pins_plugin_name);
		ltsmin_abort(3);
	} else
		module = metamodule;

	// The meta-module term is no longer needed
	delete context;

	return metamodule;
}

inline void
MaudePINSModule::readAtomicProps(const char* apspec) {
	string atomicPropsStr = apspec == nullptr ? "" : apspec;
	size_t propsLength = atomicPropsStr.size();
	size_t lastStart = 0;
	int parenthesesLevel = 0;

	// Split the comma-separated list and store the tokens in atomicNames
	for (size_t i = 0; i < propsLength; i++)
	{
		if (atomicPropsStr[i] == '(')
			parenthesesLevel++;
		else if (atomicPropsStr[i] == ')' && parenthesesLevel > 0)
			parenthesesLevel--;
		else if (atomicPropsStr[i] == ',' && parenthesesLevel == 0) {
			atomicNames.push_back(atomicPropsStr.substr(lastStart, i - lastStart));
			lastStart = i+1;
		}
	}

	if (lastStart != propsLength)
		atomicNames.push_back(atomicPropsStr.substr(lastStart));

	const size_t nrAtomicProps = atomicNames.size();
	Printf(infoShort, "%s: %li atomic propositions entered\n", pins_plugin_name, nrAtomicProps);

	// Do the actual parsing of the atomic propositions
	// (we could have limited the parsing to the Prop kind)
	atomicProps.resize(atomicNames.size());

	for (size_t i = 0; i < nrAtomicProps; i++) {
		Term* term = maudem.parseTerm(atomicNames[i].c_str());

		if (term == nullptr) {
			Printf(assertion, "%s: cannot parse atomic proposition %s\n", pins_plugin_name, atomicNames[i].c_str());
			ltsmin_abort(3);
		}

		atomicProps[i].setNode(toDag(term));
	}
}

inline void
MaudePINSModule::readOpaqueStrats(const char* opaquestr, set<int> &opaques) {
	if (opaquestr == nullptr)
		return;

	string opaqueList = opaquestr;

	size_t lastStart = 0;
	size_t commaPos = opaqueList.find(',');

	while (commaPos != string::npos) {
		opaques.insert(Token::encode(opaqueList.substr(lastStart, commaPos - lastStart).c_str()));
		lastStart = commaPos+1;
		commaPos = opaqueList.find(',', lastStart);
	}

	if (lastStart != opaqueList.size())
		opaques.insert(Token::encode(opaqueList.substr(lastStart).c_str()));
}

inline Term*
MaudePINSModule::parseTerm(const char* txt) {
	Vector<Token> tokens;
	tokenizeString(txt, tokens);

	return module->parseTerm(tokens);
}


inline StrategyExpression*
MaudePINSModule::parseStrategyExpr(const char* txt) {
	Vector<Token> tokens;
	tokenizeString(txt, tokens);

	// Parse the given strategy expression
	StrategyExpression* strategy = module->parseStrategyExpr(tokens);

	if (strategy == nullptr) {
		Printf(assertion, "%s: cannot parse strategy\n", pins_plugin_name);
		ltsmin_abort(4);
	}

	VariableInfo vinfo;
	TermSet termSet;

	// Check whether the strategy expression is well-formed and prepare it
	if (!strategy->check(vinfo, termSet)) {
		Printf(assertion, "%s: bad strategy\n", pins_plugin_name);
		ltsmin_abort(4);
	}
	strategy->process();

	return strategy;
}

inline void
MaudePINSModule::setUpGraph(Term* term, StrategyExpression* strategy) {
	DagNode* dagNode = toDag(term);

	context = new UserLevelRewritingContext(dagNode);
	context->reduce();
	if (strategy == nullptr) {
		graph = new StateTransitionGraph(context);
		sgraph = nullptr;
	} else {
		graph = nullptr;

		set<int> opaques;
		readOpaqueStrats(popt_opaques, opaques);
		sgraph = new StrategyTransitionGraph(maudem.context, strategy, opaques, popt_biased);
	}
}

void
MaudePINSModule::expand() {
	// States are valid until proven otherwise
	validStates.push_back(true);

	// A depth-first exploration of the transition system is started
	// to separate valid from failed states (when the system is
	// controlled by a strategy). A state is valid if an infinite
	// execution can be followed from it. The principles of Tarjan's
	// algorithm apply to this case.
	expand(0);
}

bool
MaudePINSModule::expand(int state) {
	// Have we already found a valid successor for state?
	bool thisIsValid = false;
	int index = 0;	// Child index
	int nextState = maudem.getNextState(state, index);

	while (nextState != -1) {

		// This is a new state, process it
		if (nextState >= (int) validStates.size()) {
			validStates.push_back(true);

			if (expand(nextState) || (maudem.sgraph != nullptr
				&& maudem.sgraph->isSolutionState(state)))
				thisIsValid = true;
		}
		// Reached a valid state (or a state in the path)
		else if (validStates[nextState]) {
			thisIsValid = true;
		}
		// Otherwise, nextState is a failed state

		nextState = maudem.getNextState(state, ++index);
	}

	if (!thisIsValid)
		validStates[state] = false;

	return thisIsValid;
}

void
at_exit() {
	// Print the number of states and rewrites at exit
	if (maudem.graph != nullptr || maudem.sgraph != nullptr)
	{
		RewritingContext* graphContext = maudem.graph != nullptr
			? maudem.graph->getContext()
			: maudem.sgraph->getContext();
		maudem.context->addInCount(*graphContext);
		if (maudem.graph != nullptr)
		{
			Printf(infoShort, "%s: %li system states explored, %li rewrites\n",
			       pins_plugin_name,
			       maudem.graph->getNrStates(),
			       maudem.context->getTotalCount());
		}
		else
		{
			Printf(infoShort, "%s: %li system states explored (%li real), %li rewrites\n",
			       pins_plugin_name,
			       maudem.sgraph->getNrStates(),
			       maudem.sgraph->getNrRealStates(),
			       maudem.context->getTotalCount());
		}
	}
	// Print the number of rewrites only (initialization error)
	else if (maudem.context != nullptr)
		Printf(infoShort, "%s: %li rewrites\n", pins_plugin_name, maudem.context->getTotalCount());
}

inline int
getLabel(const StrategyTransitionGraph::Transition &transition) {
	switch (transition.getType()) {
		case StrategyTransitionGraph::RULE_APPLICATION:
			return transition.getRule()->getLabel().id();
		case StrategyTransitionGraph::OPAQUE_STRATEGY:
			return -(1 + transition.getStrategy()->id());
		default: // StrategyTransitionGraph::SOLUTION
			return 0;	// zero is reserved for deadlock/solution
	}
}


//
//	Definitions required for the PINS interface of LTSmin
//

extern "C" int
state_label(model_t m, int label, int* src) {
	return maudem.checkProposition(*src, label);
}

// Next state function for the strategy-free case

extern "C" int
next_state(void* model, int group, int *src, TransitionCB callback, void *arg) {

	int written = 0;
	int state = *src;
	int index = 0;
	int edgeLabel = 0;

	transition_info tinfo {&edgeLabel, 0};
	int nextState = maudem.graph->getNextState(state, index);

	while (nextState != -1) {
		#ifdef STATE_AS_CHUNK
		if (nextState >= maudem.stateCount)
			maudem.stateCount = pins_chunk_put(static_cast<model_t>(model),
							   maudem.state_type,
							   chunk_str(dagToString(maudem.graph->getStateDag(nextState)).c_str()));
		#endif
		int ruleLabel = (*maudem.graph->getStateFwdArcs(state).at(nextState).begin())->getLabel().id();

		edgeLabel = maudem.elabelTranslation[ruleLabel];
		callback(arg, &tinfo, &nextState, &written);
		nextState = maudem.graph->getNextState(state, ++index);
	}

	// Deadlock transitions are added automatically by LTSmin if using
	// the Spin LTL semantics. However, setting EXPLICIT_DEADLOCK_LOOPS
	// this can be done explicitly.
	#ifdef EXPLICIT_DEADLOCK_LOOPS
	if (index == 0) {
		written = 1;
		nextState = state;
		edgeLabel = 0;
		index++;
		callback(arg, &tinfo, &nextState, &written);
	}
	#endif

	return index;
}

// Next state function for the strategy-aware case

extern "C" int
next_state_strat(void* model, int group, int *src, TransitionCB callback, void *arg) {

	int written = 0;
	int state = *src;
	int index = 0;
	int edgeLabel = 0;

	transition_info tinfo {&edgeLabel, 0};

	int nextState = maudem.sgraph->getNextState(state, index);

	while (nextState != -1) {
		#ifdef STATE_AS_CHUNK
		if (nextState >= maudem.stateCount)
			maudem.stateCount = pins_chunk_put(static_cast<model_t>(model),
							   maudem.state_type,
							   chunk_str((dagToString(maudem.sgraph->getStateDag(nextState))
								+ to_string(nextState)).c_str()));
		#endif

		auto &transition = *maudem.sgraph->getStateFwdArcs(state).at(nextState).begin();
		edgeLabel = maudem.elabelTranslation[getLabel(transition)];
		callback(arg, &tinfo, &nextState, &written);

		nextState = maudem.sgraph->getNextState(state, ++index);
	}

	// Self-loop transition corresponding to a solution state
	if (index == 0 && maudem.sgraph->isSolutionState(state)) {
		written = 1;
		nextState = state;
		edgeLabel = 0;
		index++;
		callback(arg, &tinfo, &nextState, &written);
	}

	return index;
}

// Next state function for the strategy-aware case with failed states purged

extern "C" int
next_state_strat_purged(void* model, int group, int *src, TransitionCB callback, void *arg) {

	int written = 0;
	int state = *src;
	int index = 0;
	int count = 0;
	int edgeLabel = 0;

	transition_info tinfo {&edgeLabel, 0};

	int nextState = maudem.sgraph->getNextState(state, index);

	while (nextState != -1) {
		// The state as chunk possibility is not considered in this
		// case because state numbers are not consecutive and it might
		// not worth it

		if (maudem.validStates[nextState]) {
			auto &transition = *maudem.sgraph->getStateFwdArcs(state).at(nextState).begin();
			edgeLabel = maudem.elabelTranslation[getLabel(transition)];
			callback(arg, &tinfo, &nextState, &written);
			count++;
		}

		nextState = maudem.sgraph->getNextState(state, ++index);
	}

	// Self-loop transition corresponding to a solution state
	if (index == 0 && maudem.sgraph->isSolutionState(state)) {
		written = 1;
		nextState = state;
		edgeLabel = 0;
		count++;
		callback(arg, &tinfo, &nextState, &written);
	}

	return count;
}

// Next state function for the strategy-aware case with merged states for
// state-based logics and perhaps failed states purged

extern "C" int
next_state_strat_merged(void* model, int group, int *src, TransitionCB callback, void *arg) {

	int written = 0;
	int mergedStateIdx = *src;
	int index = 0;
	int edgeLabel = 0;

	transition_info tinfo {&edgeLabel, 0};

	const set<int> &mergedState = maudem.mergedStates[mergedStateIdx];

	// Table to index the successors by their term part
	map<int, pair<set<int>, set<int>>> successors;

	// Collect and group the successors by the underlying states
	for (int state : mergedState)
	{
		int nextState = maudem.sgraph->getNextState(state, index);

		while (nextState != -1) {
			if (popt_purge && maudem.validStates[nextState]) {
				auto &transition = *maudem.sgraph->getStateFwdArcs(state).at(nextState).begin();

				auto &[succStates, succEdges] = successors[maudem.sgraph->getStatePoint(nextState).first];
				succStates.insert(nextState);
				succEdges.insert(maudem.elabelTranslation[getLabel(transition)]);
			}
			nextState = maudem.sgraph->getNextState(state, ++index);
		}

		// Solution transition
		if (index == 0 && maudem.sgraph->isSolutionState(state)) {
			auto &[succStates, succEdges] = successors[maudem.sgraph->getStatePoint(state).first];
			succStates.insert(state);
			succEdges.insert(0);
		}
		else 
			index = 0;
	}

	// Generate the merged successors
	for (const auto &[dagNode, statesAndEdges] : successors) {
		auto it = maudem.mergedStTable.find(statesAndEdges.first);

		int nextState;

		// Check whether the augmented state has been seen before
		if (it == maudem.mergedStTable.end()) {
			#ifdef STATE_AS_CHUNK
			if (nextState >= maudem.stateCount) {
				maudem.stateCount = pins_chunk_put(static_cast<model_t>(model),
								   maudem.state_type,
								   chunk_str((dagToString(maudem.sgraph->getStateDag(*statedAndEdges.first.begin()))
									+ " @ " + to_string(nextState)).c_str()));
			}
			#endif

			nextState = maudem.mergedStates.size();
			maudem.mergedStates.push_back(statesAndEdges.first);
			maudem.mergedStTable[statesAndEdges.first] = nextState;
		}
		else
			nextState = it->second;

		for (int edge : statesAndEdges.second)
		{
			edgeLabel = edge;
			callback(arg, &tinfo, &nextState, &written);
		}
	}

	return successors.size();
}

// Next state function for the strategy-aware case with merged states for
// state and edge-based logics and perhaps failed states purged

extern "C" int
next_state_strat_merged_edge(void* model, int group, int *src, TransitionCB callback, void *arg) {

	int written = 0;
	int mergedStateIdx = *src;
	int index = 0;
	int edgeLabel = 0;

	transition_info tinfo {&edgeLabel, 0};

	const set<int> &mergedState = maudem.mergedStates[mergedStateIdx];

	// Table to index the successors by their term part and transition label
	map<pair<int, int>, set<int>> successors;

	// Collect and group the successors by the underlying states
	for (int state : mergedState)
	{
		int nextState = maudem.sgraph->getNextState(state, index);

		while (nextState != -1) {
			if (popt_purge && maudem.validStates[nextState]) {
				auto &transition = *maudem.sgraph->getStateFwdArcs(state).at(nextState).begin();

				set<int> &succStates = successors[{maudem.elabelTranslation[getLabel(transition)],
								           maudem.sgraph->getStatePoint(nextState).first}];
				succStates.insert(nextState);
			}
			nextState = maudem.sgraph->getNextState(state, ++index);
		}

		// Solution transition
		if (index == 0 && maudem.sgraph->isSolutionState(state)) {
			set<int> &succStates = successors[{0, maudem.sgraph->getStatePoint(state).first}];
			succStates.insert(state);
		}
		else 
			index = 0;
	}

	// Generate the merged successors
	for (const auto &[dagNodeAndEdge, states] : successors) {
		auto it = maudem.mergedStTable.find(states);

		int nextState;

		// Check whether the augmented state has been seen before
		if (it == maudem.mergedStTable.end()) {
			#ifdef STATE_AS_CHUNK
			if (nextState >= maudem.stateCount) {
				maudem.stateCount = pins_chunk_put(static_cast<model_t>(model),
								   maudem.state_type,
								   chunk_str((dagToString(maudem.sgraph->getStateDag(*statedAndEdges.first.begin()))
									+ " @ " + to_string(nextState)).c_str()));
			}
			#endif

			nextState = maudem.mergedStates.size();
			maudem.mergedStates.push_back(states);
			maudem.mergedStTable[states] = nextState;
		}
		else
			nextState = it->second;

		edgeLabel = dagNodeAndEdge.first;
		callback(arg, &tinfo, &nextState, &written);
	}

	return successors.size();
}

extern "C" void
pins_maude_model_init(model_t m, const char* maudef) {

	// (1) Load the selected file and the prelude
	maudem.loadMaudeFile(maudef, true);

	// (2) Select the given module
	maudem.selectModule(popt_module);

	// If the metamodule option is present, its content
	// is parsed, reduced and set as current module
	if (popt_metamodule != nullptr && *popt_metamodule != '\0')
		maudem.selectMetamodule(popt_metamodule);

	// (3) Parse the initial term
	if (popt_initial == nullptr || *popt_initial == '\0') {
		Printf(assertion, "%s: no initial term given\n", pins_plugin_name);
		ltsmin_abort(3);
	}

	Term* term = maudem.parseTerm(popt_initial);

	if (term == nullptr) {
		Printf(assertion, "%s: cannot parse initial term\n", pins_plugin_name);
		ltsmin_abort(3);
	}

	// (4) Parse the list of atomic propositions
	maudem.readAtomicProps(popt_aprops);

	// (5) Parse the strategy expression (if any)
	StrategyExpression* strategy = nullptr;

	if (popt_strategy != nullptr && *popt_strategy != '\0')
		strategy = maudem.parseStrategyExpr(popt_strategy);

	// (6) Set up the state-transition graph
	maudem.setUpGraph(term, strategy);

	// (7) Find some required data

	Term* trueConstant = maudem.parseTerm("(true).Bool");

	if (trueConstant == nullptr) {
		Printf(assertion, "%s: cannot find true constant\n", pins_plugin_name);
		ltsmin_abort(3);
	}

	maudem.trueTerm.setTerm(trueConstant);
	maudem.trueTerm.normalize();
	maudem.trueTerm.prepare();

	// Find the satisfaction symbol

	Sort* stateSort = maudem.module->findSort(Token::encode("State"));
	Sort* propSort = maudem.module->findSort(Token::encode("Prop"));
	Sort* boolSort = maudem.module->findSort(Token::encode("Bool"));

	if (stateSort == nullptr || propSort == nullptr) {
		Printf(assertion, "%s: the selected module is not prepared for model checking "
			"(missing State or Prop sorts)\n", pins_plugin_name);
		ltsmin_abort(3);
	}

	Vector<ConnectedComponent*> domain(2);
	domain[0] = stateSort->component();
	domain[1] = propSort->component();

	maudem.satisfiesSymbol = maudem.module->findSymbol(Token::encode("_|=_"), domain, boolSort->component());

	if (maudem.satisfiesSymbol == nullptr) {
		Printf(assertion, "%s: the selected module is not prepared for model checking "
			"(missing satisfaction symbol)\n", pins_plugin_name);
		ltsmin_abort(3);
	}

	// (8) Set up LTSmin environment

	lts_type_t ltstype = lts_type_create();
	// Our states are opaque indices to Maude's internal graph
	lts_type_set_state_length(ltstype, 1);
	// The types of the states should be int
	#ifdef STATE_AS_CHUNK
	int state_type = lts_type_put_type(ltstype, "state", LTStypeChunk, nullptr);
	maudem.state_type = state_type;
	#else
	int state_type = lts_type_put_type(ltstype, "state", LTStypeDirect, nullptr);
	#endif
	int rule_type = lts_type_put_type(ltstype, "action", LTStypeEnum, nullptr);
	int aprop_type = lts_type_put_type(ltstype, "prop", LTStypeBool, nullptr);
	lts_type_set_state_name(ltstype, 0, "s");
        lts_type_set_state_typeno(ltstype, 0, state_type);
	// The type of edge labels
	lts_type_set_edge_label_count (ltstype, 1);
	lts_type_set_edge_label_name(ltstype, 0, "action");
	lts_type_set_edge_label_type(ltstype, 0, "action");
	lts_type_set_edge_label_typeno(ltstype, 0, rule_type);
	// The type of state labels
	const size_t nrAtomicProps = maudem.atomicProps.size();
	lts_type_set_state_label_count (ltstype, nrAtomicProps);
	for (size_t i = 0; i < nrAtomicProps; i++) {
		lts_type_set_state_label_name(ltstype, i, maudem.atomicNames[i].c_str());
		lts_type_set_state_label_type(ltstype, i, "prop");
		lts_type_set_state_label_typeno(ltstype, i, aprop_type);
	}

	lts_type_validate(ltstype);
	GBsetLTStype(m, ltstype);
	// The initial state is always zero
	int initialIndex = 0;
	GBsetInitialState(m, &initialIndex);
	GBsetStateLabelLong(m, (get_label_method_t) state_label);

	// Select a different next-state method depending on the
	// given configuration flags
	next_method_grey_t next_method = (next_method_grey_t) next_state;

	if (strategy != nullptr)
	{
		enum { NONE, STATE, EDGE } mergeValue;

		// Check the selected merge option
		if (popt_merge == nullptr || strcmp(popt_merge, "none") == 0)
			mergeValue = NONE;
		else if (strcmp(popt_merge, "state") == 0)
			mergeValue = STATE;
		else if (strcmp(popt_merge, "edge") == 0)
			mergeValue = EDGE;
		else {
			Printf(assertion, "%s: unknown value \"%s\" for the merge-states option\n", pins_plugin_name, popt_merge);
			ltsmin_abort(2);
		}

		next_method = (next_method_grey_t) (mergeValue != NONE
				? (mergeValue == STATE ? next_state_strat_merged : next_state_strat_merged_edge)
				: (popt_purge
					? next_state_strat_purged
					: next_state_strat));

		// If merging states
		if (mergeValue != NONE) {
			maudem.mergedStates.push_back({0});
			maudem.mergedStTable[{0}] = 0;
		}

		// Expand the graph while purging failed states
		if (popt_purge)
			maudem.expand();
	}

	GBsetNextStateLong(m, next_method);

	// Use standard atexit instead of GBsetExit because the other does not work
	atexit(at_exit);

	// Register the values of the edge labels as chunks
	set<int> ruleLabels;

	for (const Rule* rl : maudem.module->getRules())
		ruleLabels.insert(rl->getLabel().id());

	pins_chunk_put(m, rule_type, chunk_str("deadlock/solution"));
	maudem.elabelTranslation[0] = 0;
	pins_chunk_put(m, rule_type, chunk_str("%unlabeled%"));
	maudem.elabelTranslation[-1] = 1;

	ruleLabels.erase(-1);

	size_t index = 2;
	for (int label : ruleLabels) {
		maudem.elabelTranslation[label] = index;
		pins_chunk_put(m, rule_type, chunk_str(Token::name(label)));
		index++;
	}

	// The same for opaque strategy identifiers
	if (popt_opaques && maudem.sgraph != nullptr) {
		ruleLabels.clear();

		for (const RewriteStrategy* strat : maudem.module->getStrategies())
			if (maudem.sgraph->isOpaque(strat->id()))
				ruleLabels.insert(strat->id());

		for (int strat : ruleLabels) {
			maudem.elabelTranslation[-(1 + strat)] = index;
			pins_chunk_put(m, rule_type, chunk_str((string("opaque_") + Token::name(strat)).c_str()));
			index++;
		}
	}

	#ifdef STATE_AS_CHUNK
	// Register the state name for the initial state
	pins_chunk_put(m, state_type, chunk_str(dagToString(maudem.context->root()).c_str()));
	#endif

	// Set up the matrices, in this case trivial ones
	matrix_t *cm = new matrix_t;
	matrix_t *sm = new matrix_t;

	dm_create(cm, 1, 1);
	dm_create(sm, nrAtomicProps, 1);

	dm_set(cm, 0, 0);

	for (size_t i = 0; i < nrAtomicProps; i++)
		dm_set(sm, i, 0);

	GBsetDMInfo(m, cm);
	GBsetStateLabelInfo(m, sm);
}
