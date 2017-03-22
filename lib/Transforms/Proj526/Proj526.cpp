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

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <sstream>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "proj526"

static cl::opt<int> LoopLine("loop_line", cl::desc("The source line number of the loop to be processed"));
static cl::opt<std::string> IterationCounts("iteration_counts", cl::desc("Possible iteration counts for specified loop"));

STATISTIC(HelloCounter, "Counts number of functions greeted");

namespace {
  // Hello - The first implementation, without getAnalysisUsage.
  struct Proj526 : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    Proj526() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      ++HelloCounter;

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
        int count = atoi(strcount.c_str());
        LoopIterationCounts.push_back(count);
      } 
      //sort LoopIterationCounts
      std::sort(LoopIterationCounts.begin(), LoopIterationCounts.end());
      errs() << "LoopLineNum: " << LoopLineNum << "\n";
      errs() << "LoopIterationCounts: ";
      for (std::vector<int>::iterator it=LoopIterationCounts.begin(); it!=LoopIterationCounts.end(); it++) {
        errs() << *it << ", ";
      }
      errs() << "\n";
      //errs() << "Hello: ";
      //errs().write_escaped(F.getName()) << '\n';
      return false;
    }
  private:
    int LoopLineNum;
    std::vector<int> LoopIterationCounts;
  };
}

char Proj526::ID = 0;
static RegisterPass<Proj526> X("proj526", "Hello World Pass");

//namespace {
//  // Hello2 - The second implementation with getAnalysisUsage implemented.
//  struct  : public FunctionPass {
//    static char ID; // Pass identification, replacement for typeid
//    Hello2() : FunctionPass(ID) {}
//
//    bool runOnFunction(Function &F) override {
//      ++HelloCounter;
//      errs() << "Hello: ";
//      errs().write_escaped(F.getName()) << '\n';
//      return false;
//    }
//
//    // We don't modify the program, so we preserve all analyses.
//    void getAnalysisUsage(AnalysisUsage &AU) const override {
//      AU.setPreservesAll();
//    }
//  };
//}
//
//char Hello2::ID = 0;
//static RegisterPass<Hello2>
//Y("hello2", "Hello World Pass (with getAnalysisUsage implemented)");
