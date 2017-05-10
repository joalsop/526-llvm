#include <vector>
#include <map>
#include <cmath>
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
//#include "llvm/DebugInfo.h"
#include "llvm/Support/CommandLine.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <sys/stat.h>
//#include "SlotTracker.h"
#include "full_trace.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugInfo.h"


#define RESULT_LINE 19134
#define FORWARD_LINE 24601
#define DEPENDENCE_LINE 24602
#define DMA_FENCE 97
#define DMA_STORE 98
#define DMA_LOAD 99
#define SINE 102
#define COSINE 103

char s_phi[] = "phi";
using namespace llvm;
using namespace std;

cl::opt<string> labelMapFilename("i",
                                 cl::desc("Name of the labelmap file."),
                                 cl::value_desc("filename"),
                                 cl::init("labelmap"));

cl::opt<bool>
    verbose("verbose-tracer",
            cl::desc("Print verbose debugging output for the tracer."),
            cl::init(false), cl::ValueDisallowed);

cl::opt<bool>
    traceAllCallees("trace-all-callees",
                    cl::desc("If specified, all functions called by functions "
                             "specified in the env variable WORKLOAD "
                             "will be traced, even if there are multiple "
                             "functions in WORKLOAD. This means that each "
                             "function can act as a \"top-level\" function."),
                    cl::init(false), cl::ValueDisallowed);

namespace {

  void split(const std::string &s, const char delim, std::set<std::string> &elems) {
      std::istringstream ss(s);
      std::string item;
      while (std::getline(ss, item, delim)) {
          elems.insert(item);
      }
  }

std::vector<std::string> intrinsics = {
  "llvm.memcpy",  // standard C lib
  "llvm.memmove",
  "llvm.memset",
  "llvm.sqrt",
  "llvm.powi",
  "llvm.sin",
  "llvm.cos",
  "llvm.pow",
  "llvm.exp",
  "llvm.exp2",
  "llvm.log",
  "llvm.log10",
  "llvm.log2",
  "llvm.fma",
  "llvm.fabs",
  "llvm.copysign",
  "llvm.floor",
  "llvm.ceil",
  "llvm.trunc",
  "llvm.rint",
  "llvm.nearbyint",
  "llvm.round",
  "llvm.bswap",  // bit manipulation
  "llvm.ctpop",
  "llvm.ctlz",
  "llvm.cttz",
  "llvm.sadd.with.overflow",  // arithmetic with overflow
  "llvm.uadd.with.overflow",
  "llvm.ssub.with.overflow",
  "llvm.usub.with.overflow",
  "llvm.smul.with.overflow",
  "llvm.umul.with.overflow",
  "llvm.fmuladd",  // specialised arithmetic
};

}// end of anonymous namespace

//static Constant *createStringArg(const char *string, Module *curr_module) {
//    Constant *v_string =
//        ConstantDataArray::getString(curr_module->getContext(), string, true);
//    ArrayType *ArrayTy_0 = ArrayType::get(
//        IntegerType::get(curr_module->getContext(), 8), (strlen(string) + 1));
//    GlobalVariable *gvar_array = new GlobalVariable(
//        *curr_module, ArrayTy_0, true, GlobalValue::PrivateLinkage, 0, ".str");
//    gvar_array->setInitializer(v_string);
//    std::vector<Constant *> indices;
//    ConstantInt *zero = ConstantInt::get(curr_module->getContext(),
//                                         APInt(32, StringRef("0"), 10));
//    indices.push_back(zero);
//    indices.push_back(zero);
//    return ConstantExpr::getGetElementPtr(gvar_array, indices);
//}

int getMemSize(Type *T) {
  int size = 0;
  if (T->isPointerTy())
    return 8 * 8;
  else if (T->isFunctionTy())
    size = 0;
  else if (T->isLabelTy())
    size = 0;
  else if (T->isStructTy()) {
    StructType *S = dyn_cast<StructType>(T);
    for (unsigned i = 0; i != S->getNumElements(); i++) {
      Type *t = S->getElementType(i);
      size += getMemSize(t);
    }
  } else if (T->isFloatingPointTy()) {
    switch (T->getTypeID()) {
    case llvm::Type::HalfTyID: ///<  1: 16-bit floating point typ
      size = 16;
      break;
    case llvm::Type::FloatTyID: ///<  2: 32-bit floating point type
      size = 4 * 8;
      break;
    case llvm::Type::DoubleTyID: ///<  3: 64-bit floating point type
      size = 8 * 8;
      break;
    case llvm::Type::X86_FP80TyID: ///<  4: 80-bit floating point type (X87)
      size = 10 * 8;
      break;
    case llvm::Type::FP128TyID:
      ///<  5: 128-bit floating point type (112-bit mantissa)
      size = 16 * 8;
      break;
    case llvm::Type::PPC_FP128TyID:
      ///<  6: 128-bit floating point type (two 64-bits, PowerPC)
      size = 16 * 8;
      break;
    default:
      fprintf(stderr, "!!Unknown floating point type size\n");
      assert(false && "Unknown floating point type size");
    }
  } else if (T->isIntegerTy())
    size = cast<IntegerType>(T)->getBitWidth();
  else if (T->isVectorTy())
    size = cast<VectorType>(T)->getBitWidth();
  else if (T->isArrayTy()) {
    ArrayType *A = dyn_cast<ArrayType>(T);
    size = (int)A->getNumElements() *
           A->getElementType()->getPrimitiveSizeInBits();
  } else {
    fprintf(stderr, "!!Unknown data type: %d\n", T->getTypeID());
    assert(false && "Unknown data type");
  }

  return size;
}

Tracer::Tracer() : BasicBlockPass(ID) {}

/*
bool Tracer::doInitialization(Module &M) {
  std::string func_string;
  if (this->my_workload.empty()) {
    char* workload = getenv("WORKLOAD");
    if (workload)
        func_string = workload;
  } else {
    func_string = this->my_workload;
  }

  auto &llvm_context = M.getContext();
  auto I1Ty = Type::getInt1Ty(llvm_context);
  auto I64Ty = Type::getInt64Ty(llvm_context);
  auto I8PtrTy = Type::getInt8PtrTy(llvm_context);
  auto VoidTy = Type::getVoidTy(llvm_context);
  auto DoubleTy = Type::getDoubleTy(llvm_context);

  // Add external trace_logger function declaratio
  TL_log0 = M.getOrInsertFunction( "trace_logger_log0", VoidTy,
      I64Ty, I8PtrTy, I8PtrTy, I8PtrTy, I64Ty, I1Ty, I1Ty, nullptr);

  TL_log_int = M.getOrInsertFunction( "trace_logger_log_int", VoidTy,
      I64Ty, I64Ty, I64Ty, I64Ty, I8PtrTy, I64Ty, I8PtrTy, nullptr);

  TL_log_double = M.getOrInsertFunction( "trace_logger_log_double", VoidTy,
      I64Ty, I64Ty, DoubleTy, I64Ty, I8PtrTy, I64Ty, I8PtrTy, nullptr);

  if (func_string.empty()) {
    errs() << "\n\nPlease set WORKLOAD as an environment variable!\n\n\n";
    return false;
  }
  std::set<std::string> user_workloads;
  split(func_string, ',', user_workloads);

  // We will instrument in top level mode if there is only one workload
  // function or if explicitly told to do so.
  is_toplevel_mode = (user_workloads.size() == 1) || traceAllCallees;
  if (is_toplevel_mode && verbose)
    std::cout << "LLVM-Tracer is instrumenting this workload in top-level mode.\n";

  st = createSlotTracker(&M);
  st->initialize();
  curr_module = &M;
  curr_function = nullptr;

  DebugInfoFinder Finder;
  Finder.processModule(M);

  #if (LLVM_VERSION == 34)
    auto it = Finder.subprogram_begin();
    auto eit = Finder.subprogram_end();
  #elif (LLVM_VERSION == 35)
    auto it = Finder.subprograms().begin();
    auto eit = Finder.subprograms().end();
  #endif

  for (auto i = it; i != eit; ++i) {
    DISubprogram S(*i);

    auto MangledName = S.getLinkageName().str();
    auto Name = S.getName().str();

    assert(Name.size() || MangledName.size());

    // Checks out whether Name or Mangled Name matches.
    auto MangledIt = user_workloads.find(MangledName);
    bool isMangledMatch = MangledIt != user_workloads.end();

    auto PreMangledIt = user_workloads.find(Name);
    bool isPreMangledMatch = PreMangledIt != user_workloads.end();

    if (isMangledMatch | isPreMangledMatch) {
      if (MangledName.empty()) {
        this->tracked_functions.insert(Name);
      } else {
        this->tracked_functions.insert(MangledName);
      }
    }
  }

  return false;
}
*/
bool Tracer::runOnBasicBlock(BasicBlock &BB) {
  return false;
}

bool runOnBasicBlock526(BasicBlock &BB, std::vector<Instruction*> pdominator_brs, std::vector<int> anti_alias_lines) {
  Function *func = BB.getParent();
  std::string funcName = func->getName().str();
  InstEnv env;
  strncpy(env.funcName, funcName.c_str(), InstEnv::BUF_SIZE);

  Function *curr_function = func;
  //if (curr_function != func) {
  //  //st->purgeFunction();
  //  //st->incorporateFunction(func);
  //  curr_function = func;
  //  slotToVarName.clear();
  //}

  //if (!is_toplevel_mode && !isTrackedFunction(funcName))
  //  return false;

  //if (isDmaFunction(funcName))
  //  return false;

  if (verbose)
    std::cout << "Tracking function: " << funcName << std::endl;

  // We have to get the first insertion point before we insert any
  // instrumentation!
  BasicBlock::iterator insertp = BB.getFirstInsertionPt();

  // set dominator branch in env
  env.pdominator_brs = pdominator_brs;

  BasicBlock::iterator itr = BB.begin();
  if (isa<PHINode>(itr))
    handlePhiNodes526(&BB, &env);

  // From this point onwards, nodes cannot be PHI nodes.
  BasicBlock::iterator nextitr;
  for (BasicBlock::iterator itr = insertp; itr != BB.end(); itr = nextitr) {
    nextitr = itr;
    Instruction *I = &(*itr);
    nextitr++;

    // Get static BasicBlock ID: produce bbid
    getBBId526(&BB, env.bbid);
    // Get static instruction ID: produce instid
    getInstId526(I, &env);
    setLineNumberIfExists526(I, &env);
    // determine whether user has specified this access is not aliased 
    env.is_aliasable = true;
    if (isa<StoreInst>(I) || isa<LoadInst>(I)) {
      env.is_aliasable = (std::find(anti_alias_lines.begin(), anti_alias_lines.end(), env.line_number) == anti_alias_lines.end());
      //errs() << *I << " is aliasable?:" << env.is_aliasable << "\n";
    }

    //errs() << "handling inst  " << *I << "\n";
    bool traceCall = true;
    if (CallInst *callI = dyn_cast<CallInst>(I)) {
      Function *fun = callI->getCalledFunction();
      // This is an indirect function invocation (i.e. through function
      // pointer). This cannot happen for code that we want to turn into
      // hardware, so skip it. Also, skip intrinsics.
      if (!fun || fun->isIntrinsic())
        continue;
      //if (!is_toplevel_mode) {
      //  std::string callfunc = fun->getName().str();
      //  traceCall = true;//traceOrNot(callfunc);
      //  if (!traceCall)
      //    continue;
      //}
    }

    if (isa<CallInst>(I) && traceCall) {
      handleCallInstruction526(I, &env);
    } else {
      handleNonPhiNonCallInstruction526(I, &env);
    }

    if (!I->getType()->isVoidTy()) {
      handleInstructionResult526(I, &(*nextitr), &env);
    }

    if (isa<AllocaInst>(I)) {
      processAllocaInstruction526(itr);
    }
  }
  return false;
}


bool Tracer::traceOrNot(std::string& func) {
  if (isTrackedFunction(func))
    return true;
  for (size_t i = 0; i < intrinsics.size(); i++) {
    if (func == intrinsics[i])
      return true;
  }
  return false;
}

bool Tracer::isTrackedFunction(std::string& func) {
  // perform search in log(n) time.
  std::set<std::string>::iterator it = this->tracked_functions.find(func);
  if (it != this->tracked_functions.end()) {
      return true;
  }
  return false;
}

void printParamLine526(Instruction *I, InstOperandParams *params, bool is_aliasable) {
  printParamLine526(I, params->param_num, params->operand_name, params->bbid,
                 params->datatype, params->datasize, params->value,
                 params->is_reg, is_aliasable, params->prev_bbid);
}

void printParamLine526(Instruction *I, int param_num, const char* reg_id,
                            const char *bbId, Type::TypeID datatype,
                            unsigned datasize, Value *value, bool is_reg, bool is_aliasable,
                            const char *prev_bbid) {
  //IRBuilder<> IRB(I);
  //bool is_phi = (bbId != nullptr && strcmp(bbId, "phi") == 0);
  //Value *v_param_num = ConstantInt::get(IRB.getInt64Ty(), param_num);
  //Value *v_size = ConstantInt::get(IRB.getInt64Ty(), datasize);
  //Value *v_is_reg = ConstantInt::get(IRB.getInt64Ty(), is_reg);
  //Value *v_is_phi = ConstantInt::get(IRB.getInt64Ty(), is_phi);
  //Constant *vv_reg_id = createStringArg(reg_id, curr_module);
  //Constant *vv_prev_bbid = createStringArg(prev_bbid, curr_module);

  int is_phi = (bbId != nullptr && strcmp(bbId, "phi") == 0) ? 1 : 0;
  if (value != nullptr) {
    if (datatype == llvm::Type::IntegerTyID) {
      //Value *v_value = IRB.CreateZExt(value, IRB.getInt64Ty());
      //Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
      //                  vv_reg_id, v_is_phi, vv_prev_bbid };
      //IRB.CreateCall(TL_log_int, args);
      int int_type_val = 1;
      trace_logger_log_int(param_num, datasize, int_type_val, is_reg, reg_id, is_phi, prev_bbid);
    } else if (datatype >= llvm::Type::HalfTyID &&
               datatype <= llvm::Type::PPC_FP128TyID) {
      //Value *v_value = IRB.CreateFPExt(value, IRB.getDoubleTy());
      //Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
      //                  vv_reg_id, v_is_phi, vv_prev_bbid };
      //IRB.CreateCall(TL_log_double, args);
      int weird_type_val = 2;
      trace_logger_log_double(param_num, datasize, weird_type_val, is_reg, reg_id, is_phi, prev_bbid);
    } else if (datatype == llvm::Type::PointerTyID) {
      //Value *v_value = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
      //Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
      //                  vv_reg_id, v_is_phi, vv_prev_bbid };
      //IRB.CreateCall(TL_log_int, args);
      // if specified as unaliasable, make this address unique
      // otherwise set it to 0x0 (assumed to always alias with all other aliasable)
      long address = 0;
      if (!is_aliasable) {
        address = unique_mem_address;
        //datasize is in bits so divide by 8
        unique_mem_address+=datasize/8;
        //errs() << "non-aliasable inst " << *I << "\n";
      }
      else {
        //errs() << "aliasable inst " << *I << "\n";
      }
      trace_logger_log_int(param_num, datasize, address, is_reg, reg_id, is_phi, prev_bbid);
    } else {
      fprintf(stderr, "normal data else: %d, %s\n", datatype, reg_id);
    }
  } else {
    //Value *v_value = ConstantInt::get(IRB.getInt64Ty(), 0);
    //Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
    //                  vv_reg_id, v_is_phi, vv_prev_bbid };
    //IRB.CreateCall(TL_log_int, args);
    int null_type_val = 3;
    trace_logger_log_int(param_num, datasize, null_type_val, is_reg, reg_id, is_phi, prev_bbid);
  }
}

//void Tracer::printParamLine(Instruction *I, InstOperandParams *params) {
//  printParamLine(I, params->param_num, params->operand_name, params->bbid,
//                 params->datatype, params->datasize, params->value,
//                 params->is_reg, params->prev_bbid);
//}
//
//void Tracer::printParamLine(Instruction *I, int param_num, const char *reg_id,
//                            const char *bbId, Type::TypeID datatype,
//                            unsigned datasize, Value *value, bool is_reg,
//                            const char *prev_bbid) {
//  IRBuilder<> IRB(I);
//  bool is_phi = (bbId != nullptr && strcmp(bbId, "phi") == 0);
//  Value *v_param_num = ConstantInt::get(IRB.getInt64Ty(), param_num);
//  Value *v_size = ConstantInt::get(IRB.getInt64Ty(), datasize);
//  Value *v_is_reg = ConstantInt::get(IRB.getInt64Ty(), is_reg);
//  Value *v_is_phi = ConstantInt::get(IRB.getInt64Ty(), is_phi);
//  Constant *vv_reg_id = createStringArg(reg_id, curr_module);
//  Constant *vv_prev_bbid = createStringArg(prev_bbid, curr_module);
//
//  if (value != nullptr) {
//    if (datatype == llvm::Type::IntegerTyID) {
//      Value *v_value = IRB.CreateZExt(value, IRB.getInt64Ty());
//      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
//                        vv_reg_id, v_is_phi, vv_prev_bbid };
//      IRB.CreateCall(TL_log_int, args);
//    } else if (datatype >= llvm::Type::HalfTyID &&
//               datatype <= llvm::Type::PPC_FP128TyID) {
//      Value *v_value = IRB.CreateFPExt(value, IRB.getDoubleTy());
//      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
//                        vv_reg_id, v_is_phi, vv_prev_bbid };
//      IRB.CreateCall(TL_log_double, args);
//    } else if (datatype == llvm::Type::PointerTyID) {
//      Value *v_value = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
//      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
//                        vv_reg_id, v_is_phi, vv_prev_bbid };
//      IRB.CreateCall(TL_log_int, args);
//    } else {
//      fprintf(stderr, "normal data else: %d, %s\n", datatype, reg_id);
//    }
//  } else {
//    Value *v_value = ConstantInt::get(IRB.getInt64Ty(), 0);
//    Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
//                      vv_reg_id, v_is_phi, vv_prev_bbid };
//    IRB.CreateCall(TL_log_int, args);
//  }
//}

//void Tracer::printFirstLine(Instruction *I, InstEnv *env, unsigned opcode) {
//  IRBuilder<> IRB(I);
//  Value *v_opty, *v_linenumber, *v_is_tracked_function,
//      *v_is_toplevel_mode;
//  v_opty = ConstantInt::get(IRB.getInt64Ty(), opcode);
//  v_linenumber = ConstantInt::get(IRB.getInt64Ty(), env->line_number);
//
//  // These two parameters are passed so the instrumented binary can be run
//  // completely standalone (does not need the WORKLOAD env variable
//  // defined).
//  v_is_tracked_function = ConstantInt::get(
//      IRB.getInt1Ty(),
//      (tracked_functions.find(env->funcName) != tracked_functions.end()));
//  v_is_toplevel_mode = ConstantInt::get(IRB.getInt1Ty(), is_toplevel_mode);
//  Constant *vv_func_name = createStringArg(env->funcName, curr_module);
//  Constant *vv_bb = createStringArg(env->bbid, curr_module);
//  Constant *vv_inst = createStringArg(env->instid, curr_module);
//  Value *args[] = { v_linenumber,      vv_func_name, vv_bb,
//                    vv_inst,           v_opty,       v_is_tracked_function,
//                    v_is_toplevel_mode };
//  IRB.CreateCall(TL_log0, args);
//}

void printFirstLine526(Instruction *I, InstEnv *env, unsigned opcode) {
  //IRBuilder<> IRB(I);
  //Value *v_opty, *v_linenumber, *v_is_tracked_function,
  //    *v_is_toplevel_mode;
  //v_opty = ConstantInt::get(IRB.getInt64Ty(), opcode);
  //v_linenumber = ConstantInt::get(IRB.getInt64Ty(), env->line_number);

  //// These two parameters are passed so the instrumented binary can be run
  //// completely standalone (does not need the WORKLOAD env variable
  //// defined).
  //v_is_tracked_function = ConstantInt::get(
  //    IRB.getInt1Ty(),
  //    (tracked_functions.find(env->funcName) != tracked_functions.end()));
  //v_is_toplevel_mode = ConstantInt::get(IRB.getInt1Ty(), is_toplevel_mode);
  //Constant *vv_func_name = createStringArg(env->funcName, curr_module);
  //Constant *vv_bb = createStringArg(env->bbid, curr_module);
  //Constant *vv_inst = createStringArg(env->instid, curr_module);
  //Value *args[] = { v_linenumber,      vv_func_name, vv_bb,
  //                  vv_inst,           v_opty,       v_is_tracked_function,
  //                  v_is_toplevel_mode };
  //IRB.CreateCall(TL_log0, args);
  bool is_tracked_function = true;//(tracked_functions.find(env->funcName) != tracked_functions.end());
  
  int inst_count = trace_logger_log0(env->line_number, env->funcName, env->bbid, env->instid, opcode, is_tracked_function, true);//is_toplevel_mode);
  inst_map[I] = inst_count;
  //errs() << "setting inst_map:" << *I << "\n";
}

bool Tracer::getInstId(Instruction *I, InstEnv* env) {
  return getInstId(I, env->bbid, env->instid, &(env->instc));
}

bool getInstId526(Instruction *I, InstEnv* env) {
  return getInstId526(I, env->bbid, env->instid, &(env->instc));
}

bool Tracer::setOperandNameAndReg(Instruction *I, InstOperandParams *params) {return false;}

bool setOperandNameAndReg526(Instruction *I, InstOperandParams *params) {
  // This instruction operand must have a name or a local slot. If it does not,
  // then it will try to construct an artificial name, which will fail because
  // bbid and instc are NULL.
  params->is_reg = getInstId526(I, nullptr, params->operand_name, nullptr);
  return params->is_reg;
}

//bool Tracer::getInstId(Instruction *I, char *bbid, char *instid, int *instc) {
//  assert(instid != nullptr);
//  if (I->hasName()) {
//    strcpy(instid, I->getName().str().c_str());
//    return true;
//  }
//  int id = st->getLocalSlot(I);
//  if (slotToVarName.find(id) != slotToVarName.end()) {
//    strcpy(instid, slotToVarName[id].c_str());
//    return true;
//  }
//  if (id >= 0) {
//    sprintf(instid, "%d", id);
//    return true;
//  }
//  if (id == -1) {
//    // This instruction does not produce a value in a new register.
//    // Examples include branches, stores, calls, returns.
//    // instid is constructed using the bbid and a monotonically increasing
//    // instruction count.
//    assert(bbid != nullptr && instc != nullptr);
//    sprintf(instid, "%s-%d", bbid, *instc);
//    (*instc)++;
//    return true;
//  }
//  return false;
//}

bool getInstId526(Instruction *I, char *bbid, char *instid, int *instc) {
  assert(instid != nullptr);
  if (I->hasName()) {
    strcpy(instid, I->getName().str().c_str());
    return true;
  }
  else {
    // This instruction does not produce a value in a new register.
    // Examples include branches, stores, calls, returns.
    // instid is constructed using the bbid and a monotonically increasing
    // instruction count.
    assert(bbid != nullptr && instc != nullptr);
    sprintf(instid, "%s-%d", bbid, *instc);
    (*instc)++;
    return true;
  }
  return false;
}

void Tracer::processAllocaInstruction(BasicBlock::iterator it) {}

void processAllocaInstruction526(BasicBlock::iterator it) {
  AllocaInst *alloca = dyn_cast<AllocaInst>(it);
  // If this instruction's output register is already named, then we don't need
  // to do any more searching.
  if (!alloca->hasName()) {
    ///int alloca_id = st->getLocalSlot(alloca);
    Value * alloca_ptr = dynamic_cast<Value*>(alloca);
    bool found_debug_declare = false;
    // The debug declare call is not guaranteed to come right after the alloca.
    //while (!found_debug_declare && !it->isTerminator()) {
    //  it++;
    //  Instruction *I = &(*it);
    //  DbgDeclareInst *di = dyn_cast<DbgDeclareInst>(I);
    //  if (di) {
    //    Value *wrapping_arg = di->getAddress();
    //    //int id = st->getLocalSlot(wrapping_arg);
    //    // Ensure we've found the RIGHT debug declare call by comparing the
    //    // variable whose debug information is being declared with the variable
    //    // we're looking for.
    //    //if (id != alloca_id)
    //    if (wrapping_arg!=alloca_ptr)
    //      continue;

    //    MDNode *md = di->getVariable();
    //    // The name of the variable is the third operand of the metadata node.
    //    Value *name_operand = md->getOperand(2);
    //    std::string name = name_operand->getName().str();
    //    //slotToVarName[id] = name;
    //    found_debug_declare = true;
    //  }
    //}
  }
}

//void Tracer::getBBId(Value *BB, char *bbid) {
//  int id;
//  id = st->getLocalSlot(BB);
//  bool hasName = BB->hasName();
//  if (hasName)
//    strcpy(bbid, (char *)BB->getName().str().c_str());
//  if (!hasName && id >= 0)
//    sprintf(bbid, "%d", id);
//  // Something went wrong.
//  assert((hasName || id != -1) &&
//         "This basic block does not have a name or a ID!\n");
//}

void getBBId526(Value *BB, char *bbid) {
  bool hasName = BB->hasName();
  if (hasName)
    strcpy(bbid, (char *)BB->getName().str().c_str());
  else
    assert(0);
}

std::string remove526Suffix(std::string name) {
  size_t suffix_start = name.find("__526");
  if (suffix_start != string::npos) {
    name = name.substr(0, suffix_start);
  }
  return name;
}

bool Tracer::isDmaFunction(std::string& funcName) {
  return (funcName == "dmaLoad" ||
          funcName == "dmaStore" ||
          funcName == "dmaFence");
}

void Tracer::setLineNumberIfExists(Instruction *I, InstEnv *env) {
}

void setLineNumberIfExists526(Instruction *I, InstEnv *env) {

  if (DILocation *Loc = I->getDebugLoc()) {
    env->line_number = Loc->getLine();
  }
  else {
    env->line_number = -1;
  }
}

void Tracer::handlePhiNodes(BasicBlock* BB, InstEnv* env) {}

// Handle all phi nodes at the beginning of a basic block.
void handlePhiNodes526(BasicBlock* BB, InstEnv* env) {
  BasicBlock::iterator insertp = BB->getFirstInsertionPt();

  char prev_bbid[InstEnv::BUF_SIZE];
  char operR[InstEnv::BUF_SIZE];

  for (BasicBlock::iterator itr = BB->begin(); isa<PHINode>(itr); itr++) {
    Instruction *I = &(*itr);
    InstOperandParams params;
    params.prev_bbid = prev_bbid;
    params.operand_name = operR;
    params.bbid = s_phi;

    Value *curr_operand = nullptr;

    getBBId526(BB, env->bbid);
    getInstId526(&(*itr), env);
    setLineNumberIfExists526(I, env);

    //printFirstLine526(&(*insertp), env, itr->getOpcode());
    printFirstLine526(&(*itr), env, itr->getOpcode());

    // Print each operand.
    //errs() << "Process Phi node " << *I << "\n";
    int num_of_operands = itr->getNumOperands();
    if (num_of_operands > 0) {
      for (int i = num_of_operands - 1; i >= 0; i--) {
        BasicBlock *prev_bblock =
            (dyn_cast<PHINode>(itr))->getIncomingBlock(i);
        getBBId526(prev_bblock, params.prev_bbid);
        curr_operand = itr->getOperand(i);
        params.param_num = i + 1;
        params.setDataTypeAndSize(curr_operand);

        if (Instruction *Iop = dyn_cast<Instruction>(curr_operand)) {
          setOperandNameAndReg526(Iop, &params);
          params.value = nullptr;
          if (!curr_operand->getType()->isVectorTy()) {
            params.setDataTypeAndSize(curr_operand);
          }
          // insert 'w' directive for phi instructions
          // so aladdin adds data dependency edge with phi sources
          if (inst_map.find(Iop) != inst_map.end()) {
            //errs() << "  found in inst_map:" << *Iop << ": " << inst_map[Iop] << "\n";
            trace_logger_log_int(DEPENDENCE_LINE, 0, inst_map[Iop], 0, "", 0, "");
          }
        } else {
          params.is_reg = curr_operand->hasName();
          strcpy(params.operand_name, curr_operand->getName().str().c_str());
          //errs() << "  non-inst op" << i << ": <" << params.operand_name << ">\n";
          if (!curr_operand->getType()->isVectorTy()) {
            params.value = curr_operand;
          }
        }
        //printParamLine(insertp, &params);
        printParamLine526(&(*insertp), &params);
      }
    }

    // Print result line.
    if (!itr->getType()->isVoidTy()) {
      params.is_reg = true;
      params.param_num = RESULT_LINE;
      params.operand_name = env->instid;
      params.bbid = nullptr;
      params.datatype = itr->getType()->getTypeID();
      params.datasize = getMemSize(itr->getType());
      if (itr->getType()->isVectorTy()) {
        params.value = nullptr;
      } else if (itr->isTerminator()) {
        assert(false && "It is terminator...\n");
      } else {
        params.value = &(*itr);
      }
      //printParamLine(insertp, &params);
      printParamLine526(&(*insertp), &params);
    }
    // insert 'w' directive for phi insts
    // so aladdin adds control dependency edge with predecessor branch
    errs() << "Processing phi " << *I << "\n";
    for (std::vector<Instruction*>::iterator pred_it=env->pdominator_brs.begin(); pred_it != env->pdominator_brs.end(); pred_it++) {
      errs() << "  with dominator:" << *(*pred_it) << "\n";
      assert(inst_map.find(*pred_it) != inst_map.end());
      errs() << "  found inst id:" << inst_map[*pred_it] << "\n";
      trace_logger_log_int(DEPENDENCE_LINE, 0, inst_map[*pred_it], 0, "", 0, "");
    }
  }
}

void Tracer::handleCallInstruction(Instruction* inst, InstEnv* env) {}

void handleCallInstruction526(Instruction* inst, InstEnv* env) {
  char caller_op_name[256];
  char callee_op_name[256];

  CallInst *CI = dyn_cast<CallInst>(inst);
  Function *fun = CI->getCalledFunction();
  strcpy(caller_op_name, (char *)fun->getName().str().c_str());
  unsigned opcode;
  if (fun->getName().str().find("dmaLoad") != std::string::npos)
    opcode = DMA_LOAD;
  else if (fun->getName().str().find("dmaStore") != std::string::npos)
    opcode = DMA_STORE;
  else if (fun->getName().str().find("dmaFence") != std::string::npos)
    opcode = DMA_FENCE;
  else if (fun->getName().str().find("sin") != std::string::npos)
    opcode = SINE;
  else if (fun->getName().str().find("cos") != std::string::npos)
    opcode = COSINE;
  else
    opcode = inst->getOpcode();

  printFirstLine526(inst, env, opcode);

  // Print the line that names the function being called.
  int num_operands = inst->getNumOperands();
  Value* func_name_op = inst->getOperand(num_operands - 1);
  InstOperandParams params;
  params.param_num = num_operands;
  params.operand_name = caller_op_name;
  params.bbid = nullptr;
  params.datatype = func_name_op->getType()->getTypeID();
  params.datasize = getMemSize(func_name_op->getType());
  params.value = func_name_op;
  params.is_reg = func_name_op->hasName();
  assert(params.is_reg);
  //printParamLine(inst, &params);
  printParamLine526(inst, &params);

  int call_id = 0;
  //const Function::ArgumentListType &Args(fun->getArgumentList());
  for (Function::const_arg_iterator arg_it = fun->arg_begin(),
                                    arg_end = fun->arg_end();
       arg_it != arg_end; ++arg_it, ++call_id) {
    Value* curr_operand = inst->getOperand(call_id);

    // Every argument in the function call will have two lines printed,
    // reflecting the state of the operand in the caller AND callee function.
    InstOperandParams caller;
    InstOperandParams callee;

    caller.param_num = call_id + 1;
    caller.operand_name = caller_op_name;
    caller.bbid = nullptr;

    callee.param_num = FORWARD_LINE;
    callee.operand_name = callee_op_name;
    callee.is_reg = true;
    callee.bbid = nullptr;

    caller.setDataTypeAndSize(curr_operand);
    callee.setDataTypeAndSize(curr_operand);
    strcpy(caller.operand_name, curr_operand->getName().str().c_str());
    strcpy(callee.operand_name, arg_it->getName().str().c_str());

    if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
      // This operand was produced by an instruction in this basic block (and
      // that instruction could be a phi node).
      setOperandNameAndReg526(I, &caller);

      if (!curr_operand->getType()->isVectorTy()) {
        // We don't want to print the value of a vector type.
        caller.setDataTypeAndSize(curr_operand);
        callee.setDataTypeAndSize(curr_operand);
        caller.value = curr_operand;
        callee.value = curr_operand;
      }
      printParamLine526(inst, &caller);
      printParamLine526(inst, &callee);
    } else {
      // This operand was not produced by this basic block. It may be a
      // constant, a local variable produced by a different basic block, a
      // global, a function argument, a code label, or something else.
      caller.is_reg = curr_operand->hasName();
      if (curr_operand->getType()->isVectorTy()) {
        // Nothing to do - again, don't print the value.
      } else if (curr_operand->getType()->isLabelTy()) {
        // The operand name should be the code label itself. It has no value.
        getBBId526(curr_operand, caller.operand_name);
        caller.is_reg = true;
      } else if (curr_operand->getValueID() == Value::FunctionVal) {
        // TODO: Replace this with an isa<> check instead.
        // Nothing to do.
      } else {
        // This operand does have a value to print.
        caller.value = curr_operand;
        callee.value = curr_operand;
      }
      printParamLine526(inst, &caller);
      printParamLine526(inst, &callee);
    }
  }
}

void Tracer::handleNonPhiNonCallInstruction(Instruction *inst, InstEnv* env) {}

void handleNonPhiNonCallInstruction526(Instruction *inst, InstEnv* env) {
  char op_name[256];
  //errs() << "got opcode " << inst->getOpcode() << "(" << inst->getOpcodeName() << ") from inst " << *inst << "\n";
  printFirstLine526(inst, env, inst->getOpcode());
  int num_of_operands = inst->getNumOperands();
  if (num_of_operands > 0) {
    for (int i = num_of_operands - 1; i >= 0; i--) {
      Value* curr_operand = inst->getOperand(i);

      InstOperandParams params;
      params.param_num = i + 1;
      params.operand_name = op_name;
      params.setDataTypeAndSize(curr_operand);
      params.is_reg = curr_operand->hasName();
      strcpy(params.operand_name, curr_operand->getName().str().c_str());

      if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
        setOperandNameAndReg526(I, &params);
        if (!curr_operand->getType()->isVectorTy()) {
          params.value = curr_operand;
        }
      } else {
        if (curr_operand->getType()->isVectorTy()) {
          // Nothing more to do.
        } else if (curr_operand->getType()->isLabelTy()) {
          getBBId526(curr_operand, params.operand_name);
          params.is_reg = true;
        } else if (curr_operand->getValueID() == Value::FunctionVal) {
          // TODO: Replace this with an isa<> check instead.
          // Nothing more to do.
        } else {
          params.value = curr_operand;
        }
      }
      printParamLine526(inst, &params, env->is_aliasable);
    }
  }
  // insert 'w' directive for store insts, phi insts, and terminator insts
  // so aladdin adds control dependency edge with postdominated branch
  if (isa<StoreInst>(inst) || isa<TerminatorInst>(inst) || isa<PHINode>(inst)) {
    //errs() << "Processing store/terminator/phi " << *inst << "\n";
    for (std::vector<Instruction*>::iterator it=env->pdominator_brs.begin(); it != env->pdominator_brs.end(); it++) {
      //errs() << "  with dominator:" << *(*it) << "\n";
      assert(inst_map.find(*it) != inst_map.end());
      //errs() << "  found inst id:" << inst_map[*it] << "\n";
      trace_logger_log_int(DEPENDENCE_LINE, 0, inst_map[*it], 0, "", 0, "");
    }
  }
}

void Tracer::handleInstructionResult(Instruction *inst, Instruction *next_inst,
                                     InstEnv *env) {}
void handleInstructionResult526(Instruction *inst, Instruction *next_inst,
                                     InstEnv *env) {
  InstOperandParams params;
  params.param_num = RESULT_LINE;
  params.is_reg = true;
  params.operand_name = env->instid;
  params.bbid = nullptr;
  params.setDataTypeAndSize(inst);
  if (inst->getType()->isVectorTy()) {
    // Nothing more to do.
  } else if (inst->isTerminator()) {
    assert(false && "Return instruction is terminator...\n");
  } else {
    params.value = inst;
  }
  printParamLine526(next_inst, &params);
}

LabelMapHandler::LabelMapHandler() : ModulePass(ID) {}
LabelMapHandler::~LabelMapHandler() {}

bool LabelMapHandler::runOnModule(Module &M) {
//    // Since we only want label maps to be added to the trace once at the very
//    // start, only instrument the module that contains main().
//    Function *main = M.getFunction("main");
//    if (!main)
//        return false;
//
//    bool ret = readLabelMap();
//    if (!ret)
//        return false;
//
//    if (verbose)
//      errs() << "Contents of labelmap:\n" << labelmap_str << "\n";
//
//    IRBuilder<> builder(main->front().getFirstInsertionPt());
//    Function* labelMapWriter = cast<Function>(M.getOrInsertFunction(
//        "trace_logger_write_labelmap", builder.getVoidTy(),
//        builder.getInt8PtrTy(), builder.getInt64Ty(), nullptr));
//    Value *v_size = ConstantInt::get(builder.getInt64Ty(), labelmap_str.length());
//    Constant *v_buf = createStringArg(labelmap_str.c_str(), &M);
//    Value* args[] = { v_buf, v_size };
//    builder.CreateCall(labelMapWriter, args);
//
//    return true;
  return false;
}

bool LabelMapHandler::readLabelMap() {
    std::ifstream file(labelMapFilename, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > 0) {
        char* labelmap_buf = new char[size+1];
        file.read(labelmap_buf, size);
        labelmap_buf[size] = '\0';
        if (file) {
            labelmap_str = labelmap_buf;
        }
        delete[] labelmap_buf;
    }
    file.close();
    return (labelmap_str.length() != 0);
}

void LabelMapHandler::deleteLabelMap() {
    struct stat buffer;
    if (stat(labelMapFilename.c_str(), &buffer) == 0) {
      std::remove(labelMapFilename.c_str());
    }
}

char Tracer::ID = 0;
char LabelMapHandler::ID = 0;
//static RegisterPass<Tracer>
//X("fulltrace", "Add full Tracing Instrumentation for Aladdin", false, false);
//static RegisterPass<LabelMapHandler>
//Y("labelmapwriter", "Read and store label maps into instrumented binary", false, false);
