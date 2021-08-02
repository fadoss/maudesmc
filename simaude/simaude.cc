/**
 * @file simaude.cc
 *
 * Implementation of a simulator for the probabilistic strategy language.
 */

// C++ standard libraries
#include <iostream>
#include <map>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sstream>

// Maude library headers
#include "macros.hh"

// forward declarations
#include "interface.hh"
#include "core.hh"
#include "higher.hh"
#include "freeTheory.hh"
#include "builtIn.hh"
#include "strategyLanguage.hh"
#include "mixfix.hh"

#include "global.hh"
#include "interpreter.hh"
#include "randomOpSymbol.hh"
#include "rewritingContext.hh"
#include "userLevelRewritingContext.hh"

#include "maudeStuff.hh"
#include "strategicSimulation.hh"

// the command line parser
#include <popt.h>

using namespace std;
using Counter = std::map<int, int>;

inline void
printSummary(const StrategicSimulation& search, const Counter &counter, int numSimulations, bool printCounts) {
  if (counter.empty())
    cout << "No solutions.\n";

  for (auto [index, count] : counter)
    {
      cout << "\t" << search.getStateTerm(index) << "\t";
      if (printCounts)
	cout << count << "\n";
      else
	cout << double(count) / numSimulations << "\n";
    }
}

inline void
printSummary(const map<string, int> &counter, int numSimulations, bool printCounts) {
  if (counter.empty())
    cout << "No solutions.\n";

  for (auto [key, count] : counter)
    {
      cout << "\t" << key << "\t";
      if (printCounts)
	cout << count << "\n";
      else
	cout << double(count) / numSimulations << "\n";
    }
}

void
sequentialSimulation(StrategicSimulation& search, Counter& counter, int numSimulations) {

  for (int i = 0; i < numSimulations; i++)
    {
      int solution = search.findNextResult();

      if (solution == NONE)
	break;

      auto it = counter.find(solution);

      if (it != counter.end())
	it->second++;
      else
	counter[solution] = 1;
    }

}

void liveSimulation(StrategicSimulation& search, Counter& counter, int numSimulations) {

  // This is a very quick implementation of a live simulation, it can probably be
  // improved (not being so live...)
  cout << "\x1b[2J\x1b[0;0H";

  for (int i = 0; i < numSimulations; i++)
    {
      int solution = search.findNextResult();

      if (solution == NONE)
	break;

      auto it = counter.find(solution);

      if (it != counter.end())
	it->second++;
      else
	counter[solution] = 1;

      cout << "\x1b[2J\x1b[0;0H";
      printSummary(search, counter, i + 1, false);
    }

}

inline bool
fillBuffer(int fd, char* buffer, size_t tamBuffer, char*& start, char*& end)
{
  ssize_t msglen = recv(fd, buffer, tamBuffer, 0);

  if (msglen <= 0)
    {
      perror("Error while reading input cases from parallel process:");
      return false;
    }

  start = buffer;
  end = start + msglen;
  return true;
}

void
parallelSimulation(StrategicSimulation& search, map<string, int> &pcounter, int jobs,
		   int numSimulations, int64_t &numRewrites)
{
  // The type used as size by the Counter structure
  using counter_size_t = Counter::size_type;

  // Socket through to communicate the parent process with the
  // children that actually execute the simulation
  int sockets[2];

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
    {
      cerr << "Cannot create communication socket for the parallel simulation." << endl;
      return;
    }

  // We will only read from sockets[0] and write from sockets[1]
  shutdown(sockets[0], SHUT_WR);
  shutdown(sockets[1], SHUT_RD);

  // Spawn multiple processes
  pid_t npid = 1;
  int p;

  for (p = 0; p < jobs && npid > 0; p++)
    {
      npid = fork();

      if (npid == -1)
	cerr << "Error when creating process." << endl;
    }


  // Children program
  if (npid == 0)
    {
      // Children do not use the read end of the socket
      close(sockets[0]);
      Counter counter;

      // Change the random seeds so that different simulations
      // are executed by each process
      mt19937::result_type newSeed = RandomOpSymbol::getGlobalSeed() + 7 * p;
      RandomOpSymbol::setGlobalSeed(newSeed);
      setChoiceSeed(newSeed);

      // They do a sequential simulation, sharing their work
      sequentialSimulation(search, counter, numSimulations / jobs + (p - 1 < (numSimulations % jobs)));

      // Compose the table as a binary message to be sent to the parent

      ostringstream msg;
      counter_size_t numEntries = counter.size();
      numRewrites = search.getRewriteCount();

      // < number of entries : counter_size_t > < number of rewrites : int64_t >
      msg.write((char*) &numEntries, sizeof(numEntries));
      msg.write((char*) &numRewrites, sizeof(numRewrites));

      // < number of occurences : int > < string : undetermined length > < zero bytes padding >
      for (auto [index, count] : counter)
	{
	  msg.write((char*) &count, sizeof(count));
	  msg << search.getStateTerm(index);
	  msg.put(0);

	  // Entries are padded so that ints are not split in different chunks
	  auto padding = msg.tellp() % alignof(int);
	  padding = (padding == 0 ? 0 : 4 - padding);
	  for (counter_size_t i = 0; i < padding; ++i)
	    msg.put(0);
	}

      // Send the message through the socket and finish
      string msg_str = msg.str();

      send(sockets[1], msg_str.c_str(), msg_str.size(), 0);
      exit(0);
    }
  // Parent program
  else
    {
      // The parent will not use the write end
      close(sockets[1]);
      // Buffer to read messages from the socket and its size
      constexpr size_t TAM_BUFFER = 512;
      char buffer[TAM_BUFFER + 1];
      buffer[TAM_BUFFER] = '\0';
      // Position where the content starts and first position
      // after the content in the buffer
      char* start = buffer;
      char* end = buffer;

      while (jobs > 0)
	{
	  // Read the initial chunk of the message for this child, in case it
	  // has not been read already while processing the previous child
	  if (start == end && !fillBuffer(sockets[0], buffer, TAM_BUFFER, start, end))
	    break;

	  // If the remaining buffer does not contain the complete
	  // message header, we should read the rest of it
	  else if (uintptr_t(end - start) < sizeof(counter_size_t) + sizeof(int64_t))
	    {
	      // Move the remaining buffer to the beginning
	      strncpy(buffer, start, end - start);

	      if (!fillBuffer(sockets[0], buffer + (end - start),
			      TAM_BUFFER - (end - start), start, end))
		break;

	      start = buffer;
	    }

	    // Read < number of entries : counter_size_t >
	    counter_size_t numEntries = *reinterpret_cast<counter_size_t*>(start);
	    // Read < number of rewrites : int64_t >
	    numRewrites += *reinterpret_cast<int64_t*>(start + sizeof(numEntries));
	    // Pointer to the beginning of the entry or key
	    start += sizeof(numEntries) + sizeof(numRewrites);

	    // Read every entry
	    for (counter_size_t entry = 0; entry < numEntries; entry++)
	      {
		string key;

		// If the number of occurences is not in the buffer, we refill it
		if (start == end && !fillBuffer(sockets[0], buffer, TAM_BUFFER, start, end))
		  {
		    jobs = 0;
		    break;
		  }

		// Read < number of occurences : int >
		int count = *reinterpret_cast<const int*>(start);
		start += sizeof(count);

		// If the string is not in the buffer, we refill it
		if (start == end && !fillBuffer(sockets[0], buffer, TAM_BUFFER, start, end))
		  {
		    jobs = 0;
		    break;
		  }

		char* pointer = start;

		// Read < string : undetermined length >
		while (*pointer != 0)
		  {
		    pointer++;
		    // The buffer is exhausted (without finding a zero),
		    // so we read another chunk
		    if (pointer == end)
		      {
			key += start;

			if (!fillBuffer(sockets[0], buffer, TAM_BUFFER, start, end))
			  {
			    entry = numEntries;
			    jobs = 0;
			    break;
			  }

			pointer = start;
		      }
		    }

		    key += start;
		    pointer += 1;

		    // Skip < zero bytes padding >
		    if ((uintptr_t) pointer % alignof(int) != 0)
		      pointer += alignof(int) - (uintptr_t) pointer % alignof(int);

		    start = pointer;

		    // Add the key and count to the table
		    auto it = pcounter.find(key);

		    if (it != pcounter.end())
		      it->second += count;
		    else
		      pcounter[key] = count;
	      }

	  int status;
	  waitpid(-1, &status, 0);
	  --jobs;
	}
    }
}

int main(int argc, const char* argv[]) {
  // Variables to hold the command-line arguments
  // (Boolean values are ints for popt)

  int numSimulations = 100;
  int numJobs = 1;
  int printCounts = false;
  int useLiveSimulation = false;
  long randomSeed = -1;
  int loadPrelude = true;
  int useMixfix = true;
  int printToStderr = false;
  const char* moduleName = nullptr;
  const char* metamoduleString = nullptr;

  poptOption simulationOptions[] {
    {"simulations", 'n', POPT_ARG_INT, &numSimulations, 0, "number of simulations", "<number>"},
    {"jobs", 'j', POPT_ARG_INT, &numJobs, 0, "number of processes", "<number>"},
    // Rather presentation than simulation options
    {"print-counts", 'c', POPT_ARG_NONE, &printCounts, 0, "print counts instead of probabilities", nullptr},
    {"live", 'l', POPT_ARG_NONE|POPT_ARGFLAG_NOT, &useLiveSimulation, 0, "show the simulation results instantaneously", nullptr},
    POPT_TABLEEND
  };

  poptOption maudeOptions[] {
    {"no-prelude", 0, POPT_ARG_NONE|POPT_ARGFLAG_NOT|POPT_ARGFLAG_ONEDASH, &loadPrelude, 0, "do not read in the standard prelude", nullptr},
    {"no-advise", 0, POPT_ARG_NONE|POPT_ARGFLAG_ONEDASH, &globalAdvisoryFlag, 0, "whether Maude warning messages are printed", nullptr},
    {"no-mixfix", 0, POPT_ARG_NONE|POPT_ARGFLAG_NOT|POPT_ARGFLAG_ONEDASH, &useMixfix, 0, "do not use mixfix notation for output", nullptr},
    //{"no-ansi-color", 0, POPT_ARG_NONE|POPT_ARGFLAG_NOT, &useANSIColor, 0, "do not use ANSI color sequences", nullptr},
    {"print-to-stderr", 0, POPT_ARG_NONE|POPT_ARGFLAG_ONEDASH, &printToStderr, 0, "print attribute should use stderr instead of stdout", nullptr},
    {"random-seed", 0, POPT_ARG_LONG|POPT_ARGFLAG_ONEDASH, &randomSeed, 0, "set seed for random number generator", "<number>"},
    POPT_TABLEEND
  };

  poptOption optionTable[] {
    {nullptr, 0, POPT_ARG_INCLUDE_TABLE, simulationOptions, 0, "Simulation options:", nullptr},
    {nullptr, 0, POPT_ARG_INCLUDE_TABLE, maudeOptions, 0, "Standard Maude options:", nullptr},
    {"module", 'm', POPT_ARG_STRING, &moduleName, 0, "name of the Maude module to be selected", "<name>"},
    {"metamodule", 'M', POPT_ARG_STRING, &metamoduleString, 0, "metamodule term to be used as module", "<term>"},
    POPT_AUTOHELP
    POPT_TABLEEND
  };

  poptContext optCon;
  optCon = poptGetContext(nullptr, argc, argv, optionTable, 0);
  poptSetOtherOptionHelp(optCon, "<maude file> <initial term> <strategy>");

  for (int result = 0; (result = poptGetNextOpt(optCon)) > 0; )
    {
      if (result < -1)
	{
	  cerr << "Error when reading arguments from the command line." << endl;
	  return 3;
	}
    }

  size_t argCount = 0;
  const char** args = poptGetArgs(optCon);

  if (args != nullptr)
    while (args[argCount] != nullptr)
      argCount++;

  if (argCount < 3)
    {
      poptPrintHelp(optCon, stderr, 0);
      return 3;
    }

  // Put into effect some Maude arguments
  if (randomSeed != -1)
    {
      RandomOpSymbol::setGlobalSeed(randomSeed);
      setChoiceSeed(randomSeed);
    }
  if (!useMixfix)
    interpreter.setPrintFlag(Interpreter::PRINT_MIXFIX, false);
  if (printToStderr)
    UserLevelRewritingContext::setPrintAttributeStream(&cerr);

  #define CHECK_NOTNULL(P, RV) if (P == nullptr) return RV;

  if (!loadMaudeFile(args[0], loadPrelude))
    return 2;

  VisibleModule* mod = selectModule(moduleName);
  CHECK_NOTNULL(mod, 2);

  if (metamoduleString != nullptr)
    {
      mod = selectMetamodule(mod, metamoduleString);
      CHECK_NOTNULL(mod, 2);
    }

  Term* initial = parseTerm(mod, args[1]);
  CHECK_NOTNULL(initial, 2);
  UserLevelRewritingContext* context = new UserLevelRewritingContext(toDag(initial));
  context->reduce();
  StrategyExpression* strat = parseStrategyExpr(mod, args[2]);
  CHECK_NOTNULL(strat, 2);
  StrategicSimulation search(context, strat);

  poptFreeContext(optCon);

  if (numJobs > 1)
    {
      map<string, int> pcounter;
      int64_t numRewrites = 0;

      parallelSimulation(search, pcounter, numJobs, numSimulations, numRewrites);
      cout << "Simulation finished after " << numSimulations << " simulations and "
	   << numRewrites << " rewrites.\n\n";
      printSummary(pcounter, numSimulations, printCounts);
    }
  else
    {
      Counter counter;

      if (useLiveSimulation)
	liveSimulation(search, counter, numSimulations);
      else
	{
	  sequentialSimulation(search, counter, numSimulations);
	  cout << "Simulation finished after " << numSimulations << " simulations and "
	       << search.getRewriteCount() << " rewrites.\n\n";
	  printSummary(search, counter, numSimulations, printCounts);
	}
    }

  return 0;
}
