// REQUIRES: system-linux
//
// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: %clangxx -O0 -finsert-def-use \
// RUN:   %t/main.cpp %t/helper.cpp -o %t/test
// RUN: rm -f %t/defuse*.trace
// RUN: cd %t && ./test
// RUN: cat %t/defuse*.trace | FileCheck %s --check-prefix=TRACE
// RUN: awk '/^EVENT / { modules[$4] = 1 } \
// RUN:   END { count = 0; for (module in modules) ++count; \
// RUN:   exit(count >= 2 ? 0 : 1) }' %t/defuse*.trace

// TRACE-DAG: EVENT {{[0-9]+}} MODULE 0x{{[0-9a-f]+}} INST
// TRACE-DAG: STORE
// TRACE-DAG: LOAD
// TRACE-DAG: EDGE
// TRACE-DAG: MEM_EDGE

//--- main.cpp

int bump(int *);
int twice(int);

int main() {
  int Value = 3;

  int Bumped = bump(&Value);
  int Doubled = twice(Value);

  return Bumped == 4 && Doubled == 8 ? 0 : 1;
}

//--- helper.cpp

int bump(int *Pointer) {
  int Value = *Pointer;
  *Pointer = Value + 1;
  return *Pointer;
}

int twice(int Value) {
  return Value * 2;
}
