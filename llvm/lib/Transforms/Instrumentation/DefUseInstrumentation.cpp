#include "llvm/Transforms/Instrumentation/DefUseInstrumentation.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/TypeSize.h"

#include <cstdint>

namespace llvm {

namespace {

bool shouldSkipFunction(const Function &F) {
  // Skip compiler-generated global initialization functions and the runtime
  // hooks themselves
  static constexpr StringLiteral SkippedFunctionPrefixes[] = {
      "__cxx_global_var_init",
      "_GLOBAL__sub_I_",
      "__def_use_",
  };

  StringRef Name = F.getName();

  for (StringLiteral Prefix : SkippedFunctionPrefixes) {
    if (Name.starts_with(Prefix))
      return true;
  }

  return false;
}

} // namespace

PreservedAnalyses
DefUseInstrumentationPass::run(Module &M, ModuleAnalysisManager &) {
  LLVMContext &Ctx = M.getContext();
  IRBuilder<> Builder(Ctx);

  DenseMap<Instruction *, uint64_t> InstIDs;
  SmallVector<Instruction *, 64> Instructions;

  Type *Int64Ty = Type::getInt64Ty(Ctx);

  FunctionType *HookType = FunctionType::get(
      Type::getVoidTy(Ctx), {Int64Ty, Int64Ty}, false);

  FunctionCallee InstHook =
      M.getOrInsertFunction("__def_use_trace_inst", HookType);
  FunctionCallee SSAUseHook =
      M.getOrInsertFunction("__def_use_trace_ssa_use", HookType);
  FunctionCallee LoadHook =
      M.getOrInsertFunction("__def_use_trace_load", HookType);
  FunctionCallee StoreHook =
      M.getOrInsertFunction("__def_use_trace_store", HookType);

  const DataLayout &DL = M.getDataLayout();

  auto InstrumentMemoryAccess =
      [&](Value *PointerOperand, Type *AccessType, FunctionCallee Hook) {
        const TypeSize AccessSize = DL.getTypeStoreSize(AccessType);

        // The runtime currently accepts only a fixed byte size
        if (AccessSize.isScalable())
          return;

        Value *Address =
            Builder.CreatePtrToInt(PointerOperand, Int64Ty);

        Builder.CreateCall(
            Hook,
            {Address, Builder.getInt64(AccessSize.getFixedValue())});
      };

  GlobalVariable *ModuleTokenGV =
      M.getGlobalVariable("__def_use_module_token", true);

  if (!ModuleTokenGV) {
    ModuleTokenGV = new GlobalVariable(
        M,
        Type::getInt8Ty(Ctx),
        false,
        GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt8Ty(Ctx), 0),
        "__def_use_module_token");
  }

  Constant *ModuleToken =
      ConstantExpr::getPtrToInt(ModuleTokenGV, Int64Ty);

  uint64_t NextInstID = 0;

  // Collect original instructions before modifying the IR. This prevents
  // newly inserted tracing calls from being instrumented by this pass
  for (Function &F : M) {
    if (F.isDeclaration() || shouldSkipFunction(F))
      continue;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        // PHI nodes require predecessor-specific instrumentation and are not
        // handled by the current implementation
        if (isa<PHINode>(I))
          continue;

        Instructions.push_back(&I);
        InstIDs[&I] = NextInstID++;
      }
    }
  }

  for (Instruction *I : Instructions) {
    Builder.SetInsertPoint(I);

    const uint64_t InstID = InstIDs.lookup(I);

    Builder.CreateCall(
        InstHook,
        {ModuleToken, Builder.getInt64(InstID)});

    if (auto *LI = dyn_cast<LoadInst>(I)) {
      InstrumentMemoryAccess(
          LI->getPointerOperand(),
          LI->getType(),
          LoadHook);
    } else if (auto *SI = dyn_cast<StoreInst>(I)) {
      InstrumentMemoryAccess(
          SI->getPointerOperand(),
          SI->getValueOperand()->getType(),
          StoreHook);
    }

    for (Use &Operand : I->operands()) {
      auto *Def = dyn_cast<Instruction>(Operand.get());

      if (!Def || !InstIDs.contains(Def))
        continue;

      const uint64_t DefID = InstIDs.lookup(Def);

      Builder.CreateCall(
          SSAUseHook,
          {ModuleToken, Builder.getInt64(DefID)});
    }
  }

  return PreservedAnalyses::none();
}

} // namespace llvm