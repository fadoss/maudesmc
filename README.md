Model checker for Maude systems controlled by strategies
========================================================

An extension of the Maude LTL model checker for rewriting systems controlled by the Maude strategy language. CTL\* and μ-calculus properties can also be checked via a language module for the [LTSmin model checker](https://github.com/utwente-fmt/ltsmin).

This repository includes the [Maude](https://maude.cs.illinois.edu) source code with the aforementioned extensions, organized in different branches.

Branches
--------

* `upstream` is the official Maude version that can be downloaded from [github.com/maude-lang/Maude](https://github.com/maude-lang/Maude).
* `master` extends the Maude LTL model checker to support systems controlled by the strategy language.
* `branching` contains a language module for the language-independent model checker LTSmin. It can be used to model check CTL\* and μ-calculus properties on Maude specifications, controlled by strategies or not.
* `probabilistic` contains the probabilistic extension of the Maude strategy language.
* `windows` includes some patches that allow compiling Maude for Windows using [MinGW](https://www.mingw-w64.org).
* `specials` extends the interpreter with a hook for custom special operators through the [language bindings](https://github.com/fadoss/maude-bindings).

Build
-----

The Maude interpreter including the strategy-aware model checker can be built using the following command sequence:

```
autoreconf -i
./configure
make
```

The `configure` script admits the same options as the official Maude version. However, the command-line editing library [Tecla](https://sites.astro.caltech.edu/~mcs/tecla/) has been replaced by GNU readline. Some other libraries (GMP, BuDDy, libsigsegv, and CVC4 or Yices2 for optional SMT support) and tools (flex and bison) are required to build Maude, but they are easily available in most common Linux distributions and Mac package managers.

### Building the language module for LTSmin

The build system for the language module uses [Meson](https://mesonbuild.com) instead of Autotools. Since both the interpreter and the plugin rely on the implementation of Maude, three objects are generated so that it is shared: a common dynamic library `libmaude`, an executable for the interpreter `maude` and the LTSmin plugin `libmaudemc`.

```
meson release --buildtype=custom -Dcpp_args='-O2 -fno-stack-protector -fstrict-aliasing'
cd release
ninja
```

The available build settings are listed by running `meson configure`. Additional library and include directories can be specified with the `extra-lib-dirs` and `extra-include-dirs` options. The LTSmin headers are required to build the language module. They are included in the release packages available at its [website](https://ltsmin.utwente.nl/), whose `include` directory should be added to the `extra-include-dirs` option if not installed in a default header location.

Usage
-----

To use the Maude interpreter, you only need to run the `maude` binary. Its prelude `prelude.maude` will be loaded automatically at startup. However, if it is not available in the expected locations (the same directory as the binary or the current working directory), you should define the `MAUDE_LIB` environment variable to the directory where it resides.

Instructions on how to use the model checker can be found [here](https://maude.ucm.es/strategies/#modelchecking).


### Using the language module for LTSmin

For model checking CTL\* and μ-calculus properties on Maude specifications, the required elements are the plugin `libmaudemc.so` and the external language-independent model checker LTSmin, which can be downloaded from its [webpage](https://ltsmin.utwente.nl/).

The recommended way for using this language module is through the unified model-checking interface [umaudemc](https://github.com/fadoss/umaudemc), which significantly simplifies the interaction. However, LTSmin `pins2lts-*` tools can be used directly with this language module, whose path should be passed to the command with the `--loader=libmaudemc.so` argument. Its various options are shown when passing the `--help` argument to the tool. The only positional argument required is the Maude source file, which should be prepared as for the builtin model checker. The initial term must be specified with the `--initial` flag, and a strategy expression may be specified to control the system with `--strat`. Atomic propositions to appear in the formula have to be specified as a comma-separated list to the `--aprops` flag, and written with all special characters escaped by a backslash in the `--invariant`, `--ltl`, `--ctl`, `--ctl-star`, `--mu` or `--mucalc` options. In addition, `MAUDE_LIB` may need to be set if the prelude is not in the expected locations.

### Using the probabilistic extension of the strategy language

The probabilistic extension of the Maude strategy language adds three new combinators, `choice`, `matchrew with weight`, and `sample` to randomly choose among multiple strategies or matches according to some weights, and to sample probabilistic distributions. These operators can be used in any strategy expression at the object level and at the metalevel. The auxiliary program `simaude` allows simulating a given probabilistic strategy many times to calculate the frequencies of its results. They can also be used with the `pcheck` and `scheck` commands of the [umaudemc](https://github.com/fadoss/umaudemc) tool.


Other resources
---------------

Extensive documentation and examples can be found at [maude.ucm.es/strategies](https://maude.ucm.es/strategies). Among those:

* The strategy-aware model checker [manual](https://maude.ucm.es/strategies/modelchecker-manual.pdf).
* The Maude manual, and its sections on the [strategy language](https://maude.lcc.uma.es/maude-manual/maude-manualch10.html) and the original [LTL model checker](https://maude.lcc.uma.es/maude-manual/maude-manualch12.html).
* The articles *[The Maude strategy language](https://doi.org/10.1016/j.jlamp.2023.100887)*, *[Model checking strategy-controlled systems in rewriting logic](https://doi.org/10.1007/s10515-021-00307-9)* (arXived [here](https://doi.org/10.48550/arXiv.2401.07616)), and the shorter *[Model checking strategy-controlled rewriting systems](https://doi.org/10.4230/LIPIcs.FSCD.2019.34)*.
* For the branching-time extensions, the article *[Strategies, model checking and branching-time properties in Maude](https://doi.org/10.1016/j.jlamp.2021.100700)* (arXived [here](https://doi.org/10.48550/arXiv.2401.07680)).
* For the probabilistic strategy language, the article *[QMaude: quantitative specification and verification in rewriting logic](https://doi.org/10.1007/978-3-031-27481-7_15)* (archived [here](https://hdl.handle.net/20.500.14352/101033)).

Moreover, a unified interface to the strategy-aware model checkers with support for other model-checking backends, quantitative verification, a graphical interface, and a graph generator is available [here](https://github.com/fadoss/umaudemc).

