//=- JLInstSimplify.h - Additional instsimplifyrules for julia programs =//
//
//                             Enzyme Project
//
// Part of the Enzyme Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// If using this code in an academic setting, please cite the following:
// @incollection{enzymeNeurips,
// title = {Instead of Rewriting Foreign Code for Machine Learning,
//          Automatically Synthesize Fast Gradients},
// author = {Moses, William S. and Churavy, Valentin},
// booktitle = {Advances in Neural Information Processing Systems 33},
// year = {2020},
// note = {To appear in},
// }
//
//===----------------------------------------------------------------------===//
//
// This file contains a utility LLVM pass for printing derived Activity Analysis
// results of a given function.
//
//===----------------------------------------------------------------------===//
#include <llvm/Config/llvm-config.h>

#include "llvm/ADT/SmallVector.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"

#include "llvm/IR/LegacyPassManager.h"

#include "llvm/Support/Debug.h"

#include "llvm/Analysis/TargetLibraryInfo.h"

#include "llvm-c/Core.h"
#include "llvm-c/DataTypes.h"

#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"

#include "JLInstSimplify.h"
#include "LibraryFuncs.h"
#include "Utils.h"

using namespace llvm;
#ifdef DEBUG_TYPE
#undef DEBUG_TYPE
#endif
#define DEBUG_TYPE "jl-inst-simplify"
namespace {

bool jlInstSimplify(llvm::Function &F, TargetLibraryInfo &TLI,
                    llvm::AAResults &AA, llvm::LoopInfo &LI) {
  bool changed = false;

  for (auto &BB : F)
    for (auto &I : BB) {
      if (auto FI = dyn_cast<FreezeInst>(&I)) {
        if (FI->hasOneUse()) {
          bool allBranch = true;
          for (auto user : FI->users()) {
            if (!isa<BranchInst>(user)) {
              allBranch = false;
              break;
            }
          }
          if (allBranch) {
            FI->replaceAllUsesWith(FI->getOperand(0));
            changed = true;
            continue;
          }
        }
      }
      if (I.use_empty())
        continue;

      bool legal = false;
      ICmpInst::Predicate pred;
      if (auto cmp = dyn_cast<ICmpInst>(&I)) {
        pred = cmp->getPredicate();
        legal = true;
      } else if (auto CI = dyn_cast<CallBase>(&I)) {
        if (getFuncNameFromCall(CI) == "jl_mightalias") {
#if LLVM_VERSION_MAJOR >= 14
          size_t numargs = CI->arg_size();
#else
          size_t numargs = CI->getNumArgOperands();
#endif
          if (numargs == 2 && isa<PointerType>(I.getOperand(0)->getType()) &&
              isa<PointerType>(I.getOperand(0)->getType())) {
            legal = true;
            pred = ICmpInst::Predicate::ICMP_EQ;
          }
        }
      }

      if (legal) {
        if (auto alias = arePointersGuaranteedNoAlias(
                TLI, AA, LI, I.getOperand(0), I.getOperand(1), false)) {

          bool val = *alias;
          auto repval = ICmpInst::isTrueWhenEqual(pred)
                            ? ConstantInt::get(I.getType(), 1 - val)
                            : ConstantInt::get(I.getType(), val);
          I.replaceAllUsesWith(repval);
          changed = true;
          continue;
        }
      }
    }

  return changed;
}

class JLInstSimplify final : public FunctionPass {
public:
  static char ID;
  JLInstSimplify() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
  }

  bool runOnFunction(Function &F) override {
    auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
    auto &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
    auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    return jlInstSimplify(F, TLI, AA, LI);
  }
};

} // namespace

FunctionPass *createJLInstSimplifyPass() { return new JLInstSimplify(); }

extern "C" void LLVMAddJLInstSimplifyPass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createJLInstSimplifyPass());
}

char JLInstSimplify::ID = 0;

static RegisterPass<JLInstSimplify> X("jl-inst-simplify",
                                      "JL instruction simplification");

JLInstSimplifyNewPM::Result
JLInstSimplifyNewPM::run(llvm::Function &F,
                         llvm::FunctionAnalysisManager &FAM) {
  bool changed = false;
  changed = jlInstSimplify(F, FAM.getResult<TargetLibraryAnalysis>(F),
                           FAM.getResult<AAManager>(F),
                           FAM.getResult<LoopAnalysis>(F));
  return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
llvm::AnalysisKey JLInstSimplifyNewPM::Key;
