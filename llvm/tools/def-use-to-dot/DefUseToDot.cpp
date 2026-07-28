#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct Event {
  uint64_t ID;
  std::string Module;
  uint64_t InstID;
  std::vector<std::string> Details;
};

struct Edge {
  uint64_t From;
  uint64_t To;
  bool IsMemory;
};

llvm::cl::opt<std::string> InputPath(
    llvm::cl::Positional,
    llvm::cl::desc("<trace file>"),
    llvm::cl::Required);

llvm::cl::opt<std::string> OutputPath(
    "o",
    llvm::cl::desc("Output DOT file"),
    llvm::cl::value_desc("filename"),
    llvm::cl::init("graph.dot"));

std::string escapeDotString(const std::string &Text) {
  std::string Result;

  for (char C : Text) {
    if (C == '"')
      Result += '\\';

    Result += C;
  }

  return Result;
}

bool parseTrace(std::istream &Input,
                std::map<uint64_t, Event> &Events,
                std::vector<Edge> &Edges) {
  std::optional<uint64_t> CurrentEventID;
  std::string Line;

  while (std::getline(Input, Line)) {
    if (Line.empty())
      continue;

    std::istringstream LineStream(Line);
    std::string RecordType;

    LineStream >> RecordType;

    if (RecordType == "EVENT") {
      uint64_t EventID;
      uint64_t InstID;
      std::string ModuleWord;
      std::string Module;
      std::string InstWord;

      if (!(LineStream >> EventID
                       >> ModuleWord
                       >> Module
                       >> InstWord
                       >> InstID)) {
        llvm::errs() << "error: invalid EVENT line: "
                     << Line << '\n';
        return false;
      }

      if (ModuleWord != "MODULE" || InstWord != "INST") {
        llvm::errs() << "error: invalid EVENT format: "
                     << Line << '\n';
        return false;
      }

      Events[EventID] =
          Event{EventID, Module, InstID, {}};

      CurrentEventID = EventID;
      continue;
    }

    if (RecordType == "EDGE" ||
        RecordType == "MEM_EDGE") {
      uint64_t From;
      uint64_t To;
      std::string Arrow;

      if (!(LineStream >> From >> Arrow >> To) ||
          Arrow != "->") {
        llvm::errs() << "error: invalid edge line: "
                     << Line << '\n';
        return false;
      }

      Edges.push_back(
          Edge{From, To, RecordType == "MEM_EDGE"});

      continue;
    }

    if (RecordType == "STORE" ||
        RecordType == "LOAD") {
      std::string Address;
      uint64_t Size;

      if (!(LineStream >> Address >> Size)) {
        llvm::errs() << "error: invalid memory line: "
                     << Line << '\n';
        return false;
      }

      if (!CurrentEventID.has_value()) {
        llvm::errs()
            << "error: memory operation without preceding EVENT: "
            << Line << '\n';
        return false;
      }

      auto EventIt = Events.find(*CurrentEventID);

      if (EventIt == Events.end()) {
        llvm::errs() << "error: current event was not found\n";
        return false;
      }

      std::ostringstream Detail;

      Detail << RecordType
             << ' ' << Address
             << " size=" << Size;

      EventIt->second.Details.push_back(Detail.str());
      continue;
    }

    llvm::errs() << "error: unknown trace record: "
                 << Line << '\n';
    return false;
  }

  return true;
}

void writeDot(llvm::raw_ostream &Output,
              const std::map<uint64_t, Event> &Events,
              const std::vector<Edge> &Edges) {
  Output << "digraph DefUse {\n";
  Output << "  rankdir=TB;\n";
  Output << "  node [shape=box, fontname=\"monospace\"];\n";
  Output << "  edge [fontname=\"monospace\"];\n\n";

  for (const auto &[EventID, EventData] : Events) {
    std::ostringstream Label;

    Label << "Event " << EventData.ID
          << "\\nModule " << EventData.Module
          << "\\nInst " << EventData.InstID;

    for (const std::string &Detail : EventData.Details)
      Label << "\\n" << Detail;

    Output << "  n" << EventID
           << " [label=\""
           << escapeDotString(Label.str())
           << "\"];\n";
  }

  Output << '\n';

  for (const Edge &GraphEdge : Edges) {
    Output << "  n" << GraphEdge.From
           << " -> n" << GraphEdge.To;

    if (GraphEdge.IsMemory)
      Output << " [label=\"memory\", style=dashed]";

    Output << ";\n";
  }

  Output << "}\n";
}

} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM InitLLVM(argc, argv);

  llvm::cl::ParseCommandLineOptions(
      argc, argv,
      "Convert a dynamic def-use trace to DOT\n");

  std::ifstream Input(InputPath.getValue());

  if (!Input) {
    llvm::errs() << "error: cannot open input file '"
                 << InputPath.getValue() << "'\n";
    return 1;
  }

  std::map<uint64_t, Event> Events;
  std::vector<Edge> Edges;

  if (!parseTrace(Input, Events, Edges))
    return 1;

  std::error_code EC;

  llvm::raw_fd_ostream Output(
      OutputPath.getValue(),
      EC,
      llvm::sys::fs::CD_CreateNew);

  if (EC) {
    llvm::errs() << "error: cannot create output file '"
                 << OutputPath.getValue()
                 << "': " << EC.message() << '\n';
    return 1;
  }

  writeDot(Output, Events, Edges);

  llvm::outs() << "Wrote "
               << Events.size()
               << " nodes and "
               << Edges.size()
               << " edges to "
               << OutputPath.getValue()
               << '\n';

  return 0;
}
