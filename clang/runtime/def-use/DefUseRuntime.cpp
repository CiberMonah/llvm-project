#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <utility>

namespace {

uint64_t NextEventID = 0;
uint64_t CurrentEventID = 0;

// Maps a static instruction identified by (ModuleToken, InstID) to its latest dynamic event
std::map<std::pair<uint64_t, uint64_t>, uint64_t>
    LastEventByInstruction;

// Maps a memory range identified by (Address, Size) to the event ID of its latest store
std::map<std::pair<uint64_t, uint64_t>, uint64_t>
    LastStoreEvent;

class TraceOutput {
public:
  static TraceOutput &instance() {
    static TraceOutput Output;
    return Output;
  }

  std::ostream &stream() {
    if (!File.is_open())
      return std::cerr;

    return File;
  }

  TraceOutput(const TraceOutput &) = delete;
  TraceOutput &operator=(const TraceOutput &) = delete;

private:
  TraceOutput() {
    const char *Path = std::getenv("DEF_USE_TRACE");
    File.open(Path ? Path : "defuse.trace");
  }

  std::ofstream File;
};

std::ostream &Trace() {
  return TraceOutput::instance().stream();
}

} // namespace

extern "C" void __def_use_trace_inst(uint64_t ModuleToken,
                                     uint64_t InstID) {
  CurrentEventID = NextEventID++;

  LastEventByInstruction[{ModuleToken, InstID}] = CurrentEventID;

  Trace() << "EVENT "
          << CurrentEventID
          << " MODULE 0x"
          << std::hex
          << ModuleToken
          << std::dec
          << " INST "
          << InstID
          << '\n';
}

extern "C" void __def_use_trace_ssa_use(uint64_t ModuleToken,
                                        uint64_t DefInstID) {
  auto It =
      LastEventByInstruction.find({ModuleToken, DefInstID});

  if (It == LastEventByInstruction.end())
    return;

  uint64_t DefEventID = It->second;

  Trace() << "EDGE "
          << DefEventID
          << " -> "
          << CurrentEventID
          << '\n';
}

extern "C" void __def_use_trace_store(uint64_t Address,
                                      uint64_t Size) {
  Trace() << "STORE 0x"
          << std::hex
          << Address
          << std::dec
          << " "
          << Size
          << '\n';

  std::pair<uint64_t, uint64_t> MemoryRange{Address, Size};

  LastStoreEvent[MemoryRange] = CurrentEventID;
}

extern "C" void __def_use_trace_load(uint64_t Address,
                                     uint64_t Size) {
  Trace() << "LOAD 0x"
          << std::hex
          << Address
          << std::dec
          << " "
          << Size
          << '\n';

  std::pair<uint64_t, uint64_t> MemoryRange{Address, Size};

  auto It = LastStoreEvent.find(MemoryRange);

  if (It == LastStoreEvent.end())
    return;

  uint64_t StoreEventID = It->second;

  Trace() << "MEM_EDGE "
          << StoreEventID
          << " -> "
          << CurrentEventID
          << '\n';
}