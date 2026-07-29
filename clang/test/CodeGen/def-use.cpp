// RUN: %clangxx -O0 -finsert-def-use -S -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=ENABLED
// RUN: %clangxx -O0 -S -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=DISABLED

int compute(int *Pointer, int Value) {
  *Pointer = Value + 1;
  int Loaded = *Pointer;
  return Loaded * 2;
}

// ENABLED-DAG: declare void @__def_use_trace_inst(i64, i64)
// ENABLED-DAG: declare void @__def_use_trace_ssa_use(i64, i64)
// ENABLED-DAG: declare void @__def_use_trace_load(i64, i64)
// ENABLED-DAG: declare void @__def_use_trace_store(i64, i64)

// DISABLED-LABEL: define
// DISABLED-NOT: __def_use_trace_
