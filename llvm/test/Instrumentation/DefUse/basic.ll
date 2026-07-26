; RUN: opt -passes=def-use-instrumentation -verify-each -S %s | FileCheck %s

define i32 @test(ptr %p, i32 %x) {
entry:
  store i32 %x, ptr %p
  %value = load i32, ptr %p
  %result = add i32 %value, 1
  ret i32 %result
}

; CHECK: @__def_use_module_token = internal global i8 0

; CHECK-LABEL: define i32 @test(
; CHECK: call void @__def_use_trace_inst
; CHECK: call void @__def_use_trace_store
; CHECK: store i32 %x, ptr %p

; CHECK: call void @__def_use_trace_inst
; CHECK: call void @__def_use_trace_load
; CHECK: load i32, ptr %p

; CHECK: call void @__def_use_trace_inst
; CHECK: call void @__def_use_trace_ssa_use
; CHECK: add i32 %value, 1
