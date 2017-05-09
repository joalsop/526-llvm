//===- Proj526Func.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#include "llvm/Analysis/RegionPass.h"
#include "profile-func/trace_logger.c"
#include "full-trace/full_trace.h"
#include "full-trace/full_trace.cpp"


#define DEBUG_TYPE "proj526func"

static cl::opt<std::string> UnaliasedLines("unaliased_lines", cl::desc("Source lines with accesses which we know will not alias with another access"));

namespace {
  // used for creating trace from CFG
  struct cfg_node {
    BasicBlock *block;
    std::vector<cfg_node*> pred_nodes;
    std::vector<cfg_node*> succ_nodes;
  };

  // Proj526Func - The first implementation, without getAnalysisUsage.
  struct Proj526Func : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    Proj526Func() : FunctionPass(ID) {}

    bool doInitialization(Module &M) override {
      AntiAliasingLines.clear();
      // parse comma-separated UnaliasedLines
      std::stringstream ss(UnaliasedLines);
      std::string strcount;
      while (std::getline(ss, strcount, ',')) {
        if (strcount.size() > 0) {
          size_t hyphen_loc = strcount.find("-");
          if (hyphen_loc != std::string::npos) {
            int begin = atoi(strcount.substr(0, hyphen_loc).c_str());
            int end = atoi(strcount.substr(hyphen_loc+1).c_str());
            for (int i=begin; i<=end; i++) {
              AntiAliasingLines.push_back(i);
            }
          }
          else {
            int count = atoi(strcount.c_str());
            AntiAliasingLines.push_back(count);
          }
        }
      }
      //sort AntiAliasingLines 
      std::sort(AntiAliasingLines.begin(), AntiAliasingLines.end());
      errs() << "AntiAliasingLines: ";
      for (std::vector<int>::iterator it=AntiAliasingLines.begin(); it!=AntiAliasingLines.end(); it++) {
        errs() << *it << ", ";
      }
      errs() << "\n";
    }

    bool runOnFunction(Function &F) override {
      errs() << "Proj526Func: ";
      errs().write_escaped(F.getName()) << '\n';

      // node map stores cfg_node for each block
      std::map<BasicBlock*, cfg_node> cfg_node_map;
      // trace stores topological ordering of cfg_nodes
      std::vector<cfg_node*> trace;
      
      // populate node map
      for (Function::iterator it=F.begin(); it!=F.end(); it++) {
        BasicBlock *BB = &(*it);
        cfg_node_map[BB].block = BB;
      }
      // init node map
      for (BasicBlock &BB : F) {
        for (pred_iterator Pred=pred_begin(&BB); Pred!=pred_end(&BB); Pred++) {
          BasicBlock *PBB = *Pred;
          cfg_node_map[&BB].pred_nodes.push_back(&(cfg_node_map[PBB]));
        }
        for (succ_iterator Succ=succ_begin(&BB); Succ!=succ_end(&BB); Succ++) {
          BasicBlock *SBB = *Succ;
          cfg_node_map[&BB].succ_nodes.push_back(&(cfg_node_map[SBB]));
        }
      }

      // generate topological ordering
      std::vector<cfg_node*> S;
      S.push_back(&(cfg_node_map[&(F.getEntryBlock())]));
      // Kahn's algorithm for topological sort
      while (S.size() > 0) {
        cfg_node *next = S.front();
        S.erase(S.begin());
        trace.push_back(next);
        for (std::vector<cfg_node*>::iterator it=next->succ_nodes.begin(); it!=next->succ_nodes.end(); it++) {
          // remove predecessor edge corresponding to 'next' node from each successor,
          // if this is the last predecessor edge, add successor to S
          std::vector<cfg_node*>::iterator next_it = std::find((*it)->pred_nodes.begin(), (*it)->pred_nodes.end(), next);
          (*it)->pred_nodes.erase(next_it);
          if ((*it)->pred_nodes.size() == 0) {
            S.push_back(*it);
          }
        }
      }

      int i =0;
      for (std::vector<cfg_node*>::iterator it=trace.begin(); it!=trace.end(); it++) {
        //errs() << "Basic block " << i << ":\n" << *((*it)->block) << "\n";
        errs() << "runOnBasicBlock " << i << ": " << (dynamic_cast<Value*>((*it)->block))->getName() << "\n";
        runOnBasicBlock526(*((*it)->block), AntiAliasingLines);
        i++;
      }

      cfg_node_map.clear();
      trace.clear();
      return false;
    }
  private:
    std::vector<int> AntiAliasingLines;
  };
}

char Proj526Func::ID = 0;
static RegisterPass<Proj526Func> X("proj526func", "Proj526 Function Pass");


