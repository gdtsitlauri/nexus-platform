// NEY709 project option: optimisations inside an open-source compiler.  Two passes for LLVM's new pass manager,
// loaded into `opt` as a plugin:
//
//   nexus-strength  strength reduction of integer operations by a power of two:
//                     mul x, 2^k  -> shl x, k        (also with the constant on the left)
//                     udiv x, 2^k -> lshr x, k
//                     urem x, 2^k -> and x, 2^k - 1
//                   (sdiv/srem are left alone: they round towards zero, a shift does not).  The nsw/nuw flags of a
//                   multiplication are dropped, so the result is exact modulo 2^n.
//   nexus-loops     prints every natural loop (LoopInfo) with its depth, size and the trip count that
//                   ScalarEvolution derives, the analyses behind LLVM's own unrolling.
//
//   opt -load-pass-plugin=./NexusPasses.so -passes='mem2reg,nexus-strength' in.ll -S -o out.ll
//   opt -load-pass-plugin=./NexusPasses.so -passes='mem2reg,nexus-loops' -disable-output in.ll

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

struct StrengthReduce : PassInfoMixin<StrengthReduce> {
  PreservedAnalyses run(Function& function, FunctionAnalysisManager&) {
    SmallVector<BinaryOperator*, 16> candidates;
    for (Instruction& instruction : instructions(function)) {
      if (auto* binary = dyn_cast<BinaryOperator>(&instruction)) {
        candidates.push_back(binary);
      }
    }
    unsigned replaced = 0;
    for (BinaryOperator* binary : candidates) {
      const auto opcode = binary->getOpcode();
      Value* operand = binary->getOperand(0);
      auto* constant = dyn_cast<ConstantInt>(binary->getOperand(1));
      if (opcode == Instruction::Mul && constant == nullptr) {  // multiplication commutes
        constant = dyn_cast<ConstantInt>(binary->getOperand(0));
        operand = binary->getOperand(1);
      }
      if (constant == nullptr || !constant->getValue().isPowerOf2()) {
        continue;
      }
      const unsigned shift = constant->getValue().logBase2();
      IRBuilder<> builder(binary);
      Value* result = nullptr;
      switch (opcode) {
        case Instruction::Mul:
          result = builder.CreateShl(operand, ConstantInt::get(binary->getType(), shift));
          break;
        case Instruction::UDiv:
          result = builder.CreateLShr(operand, ConstantInt::get(binary->getType(), shift));
          break;
        case Instruction::URem:
          result = builder.CreateAnd(operand, ConstantInt::get(binary->getType(), constant->getValue() - 1));
          break;
        default:
          continue;
      }
      result->takeName(binary);
      binary->replaceAllUsesWith(result);
      binary->eraseFromParent();
      ++replaced;
    }
    errs() << "nexus-strength: " << function.getName() << ": " << replaced << " operation(s) replaced\n";
    if (replaced == 0) {
      return PreservedAnalyses::all();
    }
    PreservedAnalyses preserved;
    preserved.preserveSet<CFGAnalyses>();
    return preserved;
  }
};

struct LoopReport : PassInfoMixin<LoopReport> {
  PreservedAnalyses run(Function& function, FunctionAnalysisManager& manager) {
    auto& loops = manager.getResult<LoopAnalysis>(function);
    auto& evolution = manager.getResult<ScalarEvolutionAnalysis>(function);
    for (Loop* loop : loops.getLoopsInPreorder()) {
      errs() << "nexus-loops: " << function.getName() << ": depth " << loop->getLoopDepth() << ", header ";
      loop->getHeader()->printAsOperand(errs(), false);
      errs() << ", " << loop->getNumBlocks() << " block(s), ";
      const unsigned trips = evolution.getSmallConstantTripCount(loop);
      if (trips != 0) {
        errs() << "trip count " << trips;
      } else {
        errs() << "trip count unknown";
      }
      errs() << ", backedge-taken count " << *evolution.getBackedgeTakenCount(loop) << '\n';
    }
    return PreservedAnalyses::all();
  }
};

}  // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "NexusPasses", LLVM_VERSION_STRING, [](PassBuilder& builder) {
            builder.registerPipelineParsingCallback(
                [](StringRef name, FunctionPassManager& passes, ArrayRef<PassBuilder::PipelineElement>) {
                  if (name == "nexus-strength") {
                    passes.addPass(StrengthReduce());
                    return true;
                  }
                  if (name == "nexus-loops") {
                    passes.addPass(LoopReport());
                    return true;
                  }
                  return false;
                });
          }};
}
