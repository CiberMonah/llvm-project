#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <unistd.h>


namespace {

struct MemoryRange {
  uint64_t Address;
  uint64_t Size;

  bool operator<(const MemoryRange &Other) const {
    return Address < Other.Address ||
           (Address == Other.Address && Size < Other.Size);
  }
};

uint64_t NextEventID = 0;
uint64_t CurrentEventID = 0;

// Maps a static instruction identified by (ModuleToken, InstID) to its latest
// dynamic event.
std::map<std::pair<uint64_t, uint64_t>, uint64_t>
    LastEventByInstruction;

// Maps a memory range to the event ID of its latest store.
std::map<MemoryRange, uint64_t> LastStoreEvent;

class TraceOutput {
public:
  static TraceOutput &instance() {
    static TraceOutput Output;
    return Output;
  }

  std::ostream &stream() {
    return File;
  }

  TraceOutput(const TraceOutput &) = delete;
  TraceOutput &operator=(const TraceOutput &) = delete;

private:
  TraceOutput() {
    std::string Path =
        "defuse." + std::to_string(getpid()) + ".trace";

    File.open(Path);

    if (!File) {
      std::cerr << "Failed to open def-use trace file: "
                << Path << '\n';
      std::abort();
    }
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

  MemoryRange Range{Address, Size};

  LastStoreEvent[Range] = CurrentEventID;
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

  MemoryRange Range{Address, Size};

  auto It = LastStoreEvent.find(Range);

  if (It == LastStoreEvent.end())
    return;

  uint64_t StoreEventID = It->second;

  Trace() << "MEM_EDGE "
          << StoreEventID
          << " -> "
          << CurrentEventID
          << '\n';
}