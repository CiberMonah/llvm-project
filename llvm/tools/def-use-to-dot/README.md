# Dynamic def-use graph

This project adds the `-finsert-def-use` option to Clang.

The option instruments a program and records dynamic SSA and memory
dependencies during its execution.

## Example

The commands below assume that the custom LLVM build is available in `PATH`:

```bash
export PATH=/path/to/llvm-build/bin:$PATH
```

Go to the tool directory:

```bash
cd llvm/tools/def-use-to-dot
```

Compile the example:

```bash
clang++ -O0 -finsert-def-use examples/example.cpp -o example
```

Run it:

```bash
./example
```

The runtime creates a trace file named `defuse.<pid>.trace`.

Convert the latest trace to DOT:

```bash
TRACE_FILE=$(ls -t defuse.*.trace | head -1)
def-use-to-dot "$TRACE_FILE" -o graph.dot
```

Render the graph with Graphviz:

```bash
dot -Tsvg graph.dot -o example.svg
```

## Result

![Dynamic def-use graph](docs/images/example.svg)

Each node represents one executed instruction.

Solid edges show SSA dependencies. Dashed edges marked `memory` show
dependencies between stores and later loads.

`Event` is the dynamic event ID. `Inst` is the static instruction ID inside
the module.