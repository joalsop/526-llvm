//===- Proj526.cpp - Based on example code Hello.cpp from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopUnrollAnalyzer.h"
#include "llvm/Analysis/OptimizationDiagnosticInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <climits>
#include <utility>

#include "llvm/ADT/Statistic.h"
//#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
//#include "llvm/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/SimplifyIndVar.h"
#include "llvm/Transforms/Utils/Local.h"
//#include "llvm/Transforms/Utils/UnrollLoop.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <sstream>
#include <string>
#include <stdio.h>

using namespace llvm;

#define DEBUG_TYPE "nameinsts"


namespace {
  struct NameAllInsts : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    NameAllInsts() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      int global_id = 0;
      for (BasicBlock &BB : F) {
        if (!(dynamic_cast<Value*>(&BB))->hasName()) {
          std::stringstream new_name_ss;
          new_name_ss << "526_" << global_id++;
          BB.setName(new_name_ss.str());
        }
        //errs() << "BB name: " << BB.getName() << "\n";
        for (Instruction &I : BB) {
          //errs() << "  Inst: " << I.hasName() << ", " << *(I.getType()) << ", " << I << "\n";
          if (!I.hasName() && !I.getType()->isVoidTy()) {
            std::stringstream new_name_ss;
            new_name_ss << "526_" << global_id++;
            I.setName(new_name_ss.str());
          }
          //errs() << "  Inst name: " << I.getName() << "\n";
        }
      }
      return false;
    }
  };
}

char NameAllInsts::ID = 0;
static RegisterPass<NameAllInsts> Y("nameinsts", "Name Insts Pass");

#undef DEBUG_TYPE
#define DEBUG_TYPE "proj526"
#include "Proj526Unroll.h"

static cl::opt<int> LoopLine("loop_line", cl::desc("The source line number of the loop to be processed"));
static cl::opt<std::string> IterationCounts("iteration_counts", cl::desc("Possible iteration counts for specified loop"));


namespace {
  struct Proj526 : public LoopPass {
    static char ID; // Pass identification, replacement for typeid
    Proj526() : LoopPass(ID) {}

    //bool UnrollLoop526(Loop *L, LoopInfo *LI, ScalarEvolution *SE, AssumptionCache *AC, DominatorTree *DT);

    bool doInitialization(Loop *L, LPPassManager &) override {
      if (LoopLine.getNumOccurrences() != 1 || IterationCounts.getNumOccurrences() != 1) {
        errs() << "Error: need one argument for loop_line, one argument for iteration_counts. (provided " 
               << LoopLine.getNumOccurrences() << " and " << IterationCounts.getNumOccurrences() << ", respectively)\n";
        return false;
      }

      LoopLineNum = LoopLine;
      // parse comma-separated IterationCounts
      std::stringstream ss(IterationCounts);
      std::string strcount;
      while (std::getline(ss, strcount, ',')) {
        if (strcount.size() > 0) {
          size_t hyphen_loc = strcount.find("-");
          if (hyphen_loc != std::string::npos) {
            int begin = atoi(strcount.substr(0, hyphen_loc).c_str());
            int end = atoi(strcount.substr(hyphen_loc+1).c_str());
            for (int i=begin; i<=end; i++) {
              LoopIterationCounts.push_back(i);
            }
          }
          else {
            int count = atoi(strcount.c_str());
            LoopIterationCounts.push_back(count);
          }
        }
      } 
      //sort LoopIterationCounts
      std::sort(LoopIterationCounts.begin(), LoopIterationCounts.end());
      errs() << "LoopLineNum: " << LoopLineNum << "\n";
      errs() << "LoopIterationCounts: ";
      for (std::vector<int>::iterator it=LoopIterationCounts.begin(); it!=LoopIterationCounts.end(); it++) {
        errs() << *it << ", ";
      }
      errs() << "\n";
      return true;
    }

    /// This transformation requires natural loop information & requires that
    /// loop preheaders be inserted into the CFG...
    ///
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addRequired<ScalarEvolutionWrapperPass>();
      AU.addRequired<AssumptionCacheTracker>();
      AU.addRequired<DominatorTreeWrapperPass>();
      // FIXME: Loop passes are required to preserve domtree, and for now we just
      // recreate dom info if anything gets unrolled.
      getLoopAnalysisUsage(AU);
    }

    bool runOnLoop(Loop *L, LPPassManager &LPM) override {
      int line_number = -1;
      bool loop_match = false;
      BasicBlock *BB = L->getHeader();
      for (BasicBlock::iterator I = BB->begin(), E = BB->end();
           (I != E); ++I) {
        errs() << "has metadata: " << I->hasMetadata() << ", metatdata from I: " << I->getMetadata("dbg") << ", loc:" << I->getDebugLoc() << ", " << *I << "\n";
        if (DILocation *Loc = I->getDebugLoc()) {
          line_number = Loc->getLine();
          errs() << "Got line num:" << line_number << "\n";
          if (line_number == LoopLineNum) {
            loop_match = true;
            break;
          }
        }
        else {
          errs() << "damn, can't get metatdata from I\n";
        }
      }
      if (!loop_match) {
        errs() << "Loop at " << line_number << " not a match\n";
        return false;
      }

      Function &F = *L->getHeader()->getParent();
      LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
      ScalarEvolution *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
      auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
      auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
      OptimizationRemarkEmitter ORE(&F);
      bool PreserveLCSSA = mustPreserveAnalysisID(LCSSAID);
      bool Force=false, AllowRuntime=false, AllowExpensiveTripCount=false, PreserveCondBr=false, PreserveOnlyFirst=false;
      // sorted, so back holds the max iteration number
      unsigned Count = LoopIterationCounts.back();
      unsigned TripCount = Count;
      unsigned TripMultiple=Count;
      unsigned PeelCount=0;

      errs() << "try UnrollLoop " << TripCount << " times\n";
      if (UnrollLoop(L, Count, TripCount, Force, AllowRuntime,
                      AllowExpensiveTripCount, PreserveCondBr, PreserveOnlyFirst, 
                      LoopIterationCounts,
                      TripMultiple, PeelCount, LI, SE, &DT, &AC, &ORE,
                      PreserveLCSSA)) {
        errs() << "successful loop unroll\n";
      }
      else {
        errs() << "failed loop unroll\n";
      }

      //bool prev_iter_can_exit = LoopIterationCounts.front()==i;
      //int i=1;
      //while (i <= LoopIterationCounts.back()) {
      //  // copy loop contents, change the back edge in the previous iteration to point to the new copy,
      //  // and remove the  prev iter's exit edge if an exit is not possible from that iteration
      //  copyLoop(L, prev_iter_can_exit);
      //  prev_iter_can_exit = false;
      //  if (i == LoopIterationCounts.front()) {
      //    // if this iteration matches the next valid iter count, an exit is possible
      //    LoopIterationCounts.erase(LoopIteratorCount);
      //    prev_iter_can_exit = true;
      //  }
      //  i++
      //}

      //// if loop matched, find all "latch" basic blocks
      //BasicBlock *Latch = L->getLoopLatch();

      return false;
    }
  private:
    int LoopLineNum;
    std::vector<int> LoopIterationCounts;
  };
}

char Proj526::ID = 0;
static RegisterPass<Proj526> X("proj526", "Proj526 Pass");

