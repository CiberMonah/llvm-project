#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
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

enum class RecordKind {
  Event,
  Edge,
  MemoryEdge,
  Store,
  Load,
  Unknown
};

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

RecordKind parseRecordKind(llvm::StringRef Name) {
  return llvm::StringSwitch<RecordKind>(Name)
      .Case("EVENT", RecordKind::Event)
      .Case("EDGE", RecordKind::Edge)
      .Case("MEM_EDGE", RecordKind::MemoryEdge)
      .Case("STORE", RecordKind::Store)
      .Case("LOAD", RecordKind::Load)
      .Default(RecordKind::Unknown);
}

std::string escapeDotString(const std::string &Text) {
  std::string Result;

  for (char C : Text) {
    if (C == '"')
      Result += '\\';

    Result += C;
  }

  return Result;
}

bool parseEventRecord(std::istringstream &LineStream,
                      const std::string &Line,
                      std::map<uint64_t, Event> &Events,
                      std::optional<uint64_t> &CurrentEventID) {
  uint64_t EventID;
  uint64_t InstID;
  std::string ModuleKeyword;
  std::string Module;
  std::string InstKeyword;

  if (!(LineStream >> EventID
                   >> ModuleKeyword
                   >> Module
                   >> InstKeyword
                   >> InstID)) {
    llvm::errs() << "error: invalid EVENT line: "
                 << Line << '\n';
    return false;
  }

  if (ModuleKeyword != "MODULE" || InstKeyword != "INST") {
    llvm::errs() << "error: invalid EVENT format: "
                 << Line << '\n';
    return false;
  }

  Events[EventID] = Event{EventID, Module, InstID, {}};
  CurrentEventID = EventID;

  return true;
}

bool parseEdgeRecord(std::istringstream &LineStream,
                     const std::string &Line,
                     RecordKind Kind,
                     std::vector<Edge> &Edges) {
  uint64_t From;
  uint64_t To;
  std::string Arrow;

  if (!(LineStream >> From >> Arrow >> To) || Arrow != "->") {
    llvm::errs() << "error: invalid edge line: "
                 << Line << '\n';
    return false;
  }

  Edges.push_back(
      Edge{From, To, Kind == RecordKind::MemoryEdge});

  return true;
}

bool parseMemoryRecord(std::istringstream &LineStream,
                       const std::string &Line,
                       RecordKind Kind,
                       const std::optional<uint64_t> &CurrentEventID,
                       std::map<uint64_t, Event> &Events) {
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

  llvm::StringRef Operation =
      Kind == RecordKind::Store ? "STORE" : "LOAD";

  std::ostringstream Detail;
  Detail << Operation.str()
         << ' ' << Address
         << " size=" << Size;

  EventIt->second.Details.push_back(Detail.str());

  return true;
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
    std::string RecordName;

    LineStream >> RecordName;

    RecordKind Kind = parseRecordKind(RecordName);

    switch (Kind) {
    case RecordKind::Event:
      if (!parseEventRecord(
              LineStream, Line, Events, CurrentEventID))
        return false;
      break;

    case RecordKind::Edge:
    case RecordKind::MemoryEdge:
      if (!parseEdgeRecord(LineStream, Line, Kind, Edges))
        return false;
      break;

    case RecordKind::Store:
    case RecordKind::Load:
      if (!parseMemoryRecord(
              LineStream, Line, Kind, CurrentEventID, Events))
        return false;
      break;

    case RecordKind::Unknown:
      llvm::errs() << "error: unknown trace record: "
                   << Line << '\n';
      return false;
    }
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
