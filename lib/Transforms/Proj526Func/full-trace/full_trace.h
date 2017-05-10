#include <fstream>
#include <map>
#include <string>

using namespace llvm;
using namespace std;

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"
//#include "SlotTracker.h"

#ifndef FULL_TRACE_H
#define FULL_TRACE_H

extern char s_phi[];

// Get the bitwidth of this type.
int getMemSize(Type *T);

long unique_mem_address = 0;
std::map<Instruction*, int> inst_map;

// Computed properties of an instruction commonly used by this pass's
// handling functions.
struct InstEnv {
  public:
    InstEnv() : line_number(-1), instc(0), is_aliasable(true) {}

    enum { BUF_SIZE = 256 };

    // Function name.
    char funcName[BUF_SIZE];
    // Basic block ID. See getBBId().
    char bbid[BUF_SIZE];
    // Static instruction ID. See getInstId().
    char instid[BUF_SIZE];
    // Source line number.
    int line_number;
    // Static instruction count within this basic block.
    int instc;
    // The access may be aliased (mem insts only)
    bool is_aliasable;
    // Dominator branch instruction id (used to add dependence line for stores)
    std::vector<Instruction*> pdominator_brs;
};

struct InstOperandParams {
  public:
    InstOperandParams()
        : param_num(-1), datatype(Type::VoidTyID), datasize(0), is_reg(true),
          value(nullptr), operand_name(nullptr), bbid(nullptr),
          prev_bbid(s_phi) {}

    InstOperandParams(const InstOperandParams &other)
        : param_num(other.param_num), datatype(other.datatype),
          datasize(other.datasize), is_reg(other.is_reg), value(other.value),
          operand_name(other.operand_name), bbid(other.bbid),
          prev_bbid(other.prev_bbid) {}

    void setDataTypeAndSize(Value* value) {
        datatype = value->getType()->getTypeID();
        datasize = getMemSize(value->getType());
    }

    // Operand index (first, second, etc).
    int param_num;
    // Datatype.
    Type::TypeID datatype;
    // Bitwidth of this operand.
    unsigned datasize;
    // This operand was stored in a register.
    bool is_reg;
    // Value of this operand, if it has one. PHI nodes generally do not have values.
    Value* value;

    char *operand_name;
    char *bbid;
    char *prev_bbid;
};

bool runOnBasicBlock526(BasicBlock &BB, std::vector<Instruction*> pdominator_brs, std::vector<int> anti_alias_lines);
// This is for Proj526, where we want to print the line immediately
// rather than insert a call to print a line
void printFirstLine526(Instruction *insert_point, InstEnv *env, unsigned opcode);
// This is for Proj526, where we want to print the line immediately
// rather than insert a call to print a line
void printParamLine526(Instruction *I, int param_num, const char *reg_id,
                    const char *bbId, Type::TypeID datatype,
                    unsigned datasize, Value *value, bool is_reg, bool is_aliasable,
                    const char *prev_bbid = s_phi);

void printParamLine526(Instruction *I, InstOperandParams *params, bool is_aliasable=true);
bool getInstId526(Instruction *I, char *bbid, char *instid, int *instc);
bool getInstId526(Instruction *I, InstEnv *env);
void getBBId526(Value *BB, char *bbid);
std::string remove526Suffix(std::string name);
void handlePhiNodes526(BasicBlock *BB, InstEnv *env);
void setLineNumberIfExists526(Instruction *I, InstEnv *env);
void handleCallInstruction526(Instruction *inst, InstEnv *env);
void handleNonPhiNonCallInstruction526(Instruction *inst, InstEnv *env);
void handleInstructionResult526(Instruction *inst, Instruction *next_inst,
                                 InstEnv *env);
void processAllocaInstruction526(BasicBlock::iterator it);
bool setOperandNameAndReg526(Instruction *I, InstOperandParams *params);

class Tracer : public BasicBlockPass {
  public:
    Tracer();
    virtual ~Tracer() {}
    static char ID;

    // This BasicBlockPass may be called by other program.
    // provide a way to set up workload, not just environment variable
    std::string my_workload;

    void setWorkload(std::string workload) {
        this->my_workload = workload;
    };

    //virtual bool doInitialization(Module &M);
    virtual bool runOnBasicBlock(BasicBlock &BB);

  private:
    // Instrumentation functions for different types of nodes.
    void handlePhiNodes(BasicBlock *BB, InstEnv *env);
    void handleCallInstruction(Instruction *inst, InstEnv *env);
    void handleNonPhiNonCallInstruction(Instruction *inst, InstEnv *env);
    void handleInstructionResult(Instruction *inst, Instruction *next_inst,
                                 InstEnv *env);

    // Set line number information in env for this inst if it is found.
    void setLineNumberIfExists(Instruction *I, InstEnv *env);

    // Insert instrumentation to print one line about this instruction.
    //
    // This function inserts a call to TL_log0 (the first line of output for an
    // instruction), which largely contains information about this
    // instruction's context: basic block, function, static instruction id,
    // source line number, etc.
    void printFirstLine(Instruction *insert_point, InstEnv *env, unsigned opcode);

    // Insert instrumentation to print a line about an instruction's parameters.
    //
    // Parameters may be instruction input/output operands, function call
    // arguments, return values, etc.
    //
    // Based on the value of datatype, this function inserts calls to
    // TL_log_int or TL_log_double.
    void printParamLine(Instruction *I, int param_num, const char *reg_id,
                        const char *bbId, Type::TypeID datatype,
                        unsigned datasize, Value *value, bool is_reg,
                        const char *prev_bbid = s_phi);

    void printParamLine(Instruction *I, InstOperandParams *params);


    // Should we trace this function or not?
    bool traceOrNot(std::string& func);
    // Does this function appear in our list of tracked functions?
    bool isTrackedFunction(std::string& func);
    // Is this function one of the special DMA functions?
    bool isDmaFunction(std::string& funcName);

    // Construct an ID for the given instruction.
    //
    // If the instruction produces a value in a register, this ID is set to the
    // name of the variable that contains the result or the local slot number
    // of the register, and this function returns true.
    //
    // If the instruction does not produce a value, an artificial ID is
    // constructed by concatenating the given bbid, which both must not be NULL
    // and the current instc, and this function returns false.
    //
    // The ID is stored in instid.
    bool getInstId(Instruction *I, char *bbid, char *instid, int *instc);

    // Construct an ID using the given instruction and environment.
    bool getInstId(Instruction *I, InstEnv *env);

    // Get and set the operand name for this instruction.
    bool setOperandNameAndReg(Instruction *I, InstOperandParams *params);

    // Get the variable name for this locally allocated variable.
    //
    // An alloca instruction allocates a certain amount of memory on the stack.
    // This instruction will name the register that stores the pointer
    // with the original source name if it is not locally allocated on the
    // stack. If it is a local variable, it may just store it to a temporary
    // register and name it with a slot number. In this latter case, track down
    // the original variable name using debug information and store it in the
    // slotToVarName map.
    void processAllocaInstruction(BasicBlock::iterator it);

    // Construct an ID for the given basic block.
    //
    // This ID is either the name of the basic block (e.g. ".lr.ph",
    // "_crit_edge") or the local slot number (which would appear in the IR as
    // "; <label>:N".
    //
    // The ID is stored as a string in bbid.
    void getBBId(Value *BB, char *bbid);

    // References to the logging functions.
    Value *TL_log0;
    Value *TL_log_int;
    Value *TL_log_double;

    // The current module.
    Module *curr_module;

    // The current function being instrumented.
    Function *curr_function;

    // Local slot tracker for the current function.
    //SlotTracker *st;

    // All functions we are tracking.
    std::set<std::string> tracked_functions;

    // True if WORKLOAD specifies a single function, in which case the tracer
    // will track all functions called by it (the top-level function).
    bool is_toplevel_mode;

  private:
    // Stores names of local variables allocated by alloca.
    //
    // For alloca instructions that allocate local memory, this maps the
    // register name (aka slot number) to the name of the variable itself.
    //
    // Since slot numbers are reused across functions, this has to be cleared
    // when we switch to a new function.
    std::map<unsigned, std::string> slotToVarName;
};

/* Reads a labelmap file and inserts it into the dynamic trace.
 *
 * The contents of the labelmap file are embedded into the instrumented binary,
 * so after the optimization pass is finished, the labelmap file is discarded.
 */
class LabelMapHandler : public ModulePass {
  public:
    LabelMapHandler();
    virtual ~LabelMapHandler();
    virtual bool runOnModule(Module &M);
    static char ID;

  private:
    bool readLabelMap();
    void deleteLabelMap();
    std::string labelmap_str;
};
#endif
