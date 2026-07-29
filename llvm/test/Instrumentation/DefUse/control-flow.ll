; RUN: opt -passes=def-use-instrumentation -verify-each -S %s \
; RUN:   | FileCheck %s

define i32 @control_flow(ptr %pointer, i32 %limit) {
entry:
  store i32 0, ptr %pointer, align 4
  br label %loop

loop:
  %index = phi i32 [ 0, %entry ], [ %next, %body ]
  %sum = phi i32 [ 0, %entry ], [ %new_sum, %body ]
  %condition = icmp slt i32 %index, %limit
  br i1 %condition, label %body, label %exit

body:
  %loaded = load i32, ptr %pointer, align 4
  %new_sum = add i32 %sum, %loaded
  %next = add i32 %index, 1
  store i32 %next, ptr %pointer, align 4
  br label %loop

exit:
  ret i32 %sum
}

; CHECK: @__def_use_module_token = internal global i8 0

; CHECK-LABEL: define i32 @control_flow(
; CHECK: call void @__def_use_trace_inst
; CHECK: call void @__def_use_trace_store
; CHECK: store i32 0, ptr %pointer

; PHI nodes must remain at the beginning of the basic block.
; CHECK-LABEL: loop:
; CHECK-NEXT: %index = phi i32
; CHECK-NEXT: %sum = phi i32
; CHECK-NEXT: call void @__def_use_trace_inst
; CHECK: call void @__def_use_trace_ssa_use
; CHECK: br i1

; CHECK-LABEL: body:
; CHECK: call void @__def_use_trace_inst
; CHECK: call void @__def_use_trace_load
; CHECK: load i32, ptr %pointer
; CHECK: call void @__def_use_trace_ssa_use
; CHECK: call void @__def_use_trace_store
; CHECK: store i32 %next, ptr %pointer

; CHECK-LABEL: exit:
; CHECK: call void @__def_use_trace_inst
; CHECK: ret i32 %sum
