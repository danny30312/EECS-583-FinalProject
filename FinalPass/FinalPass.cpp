#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/PredIteratorCache.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include <algorithm>
#include <utility>
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
using namespace llvm;


namespace {
struct Statics : public FunctionPass {
  static char ID;
  Statics() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<BlockFrequencyInfoWrapperPass>(); // Analysis pass to load block execution count
    AU.addRequired<BranchProbabilityInfoWrapperPass>(); // Analysis pass to load branch probability
  }

  bool runOnFunction(Function &F) override {
    BranchProbabilityInfo &bpi = getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI(); 
    BlockFrequencyInfo &bfi = getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
    
    float DynOpCounts = 0;
    float IAlu = 0;
    float FAlu = 0;
    float Mem = 0;
    float BBranch = 0;
    float UBranch = 0;
    float Other = 0;

    for (Function::iterator b = F.begin(); b != F.end(); b++) {
      for (BasicBlock::iterator i = b->begin(); i != b->end(); i++) {
        uint64_t blockCount = bfi.getBlockProfileCount(&*b).getValue();

        DynOpCounts += blockCount;

        int inst = i->getOpcode();
        if (inst == Instruction::Add
          ||inst == Instruction::Sub
          ||inst == Instruction::Mul
          ||inst == Instruction::UDiv
          ||inst == Instruction::SDiv
          ||inst == Instruction::URem
          ||inst == Instruction::Shl
          ||inst == Instruction::LShr
          ||inst == Instruction::AShr
          ||inst == Instruction::And
          ||inst == Instruction::Or
          ||inst == Instruction::Xor
          ||inst == Instruction::ICmp
          ||inst == Instruction::SRem) {IAlu += blockCount;}

        else if (inst == Instruction::FAdd
          ||inst == Instruction::FSub
          ||inst == Instruction::FMul
          ||inst == Instruction::FDiv
          ||inst == Instruction::FRem
          ||inst == Instruction::FCmp) {FAlu += blockCount;}

        else if (inst == Instruction::Alloca
          ||inst == Instruction::Load
          ||inst == Instruction::Store
          ||inst == Instruction::GetElementPtr
          ||inst == Instruction::Fence
          ||inst == Instruction::AtomicCmpXchg
          ||inst == Instruction::AtomicRMW) {Mem += blockCount;}

        else if (inst == Instruction::Br
          ||inst == Instruction::Switch
          ||inst == Instruction::IndirectBr){
            if (bpi.getHotSucc((BasicBlock*) &*b)){
		          BBranch += blockCount;
	          }
	          else {
		          UBranch += blockCount;
	          }
          }

        else{Other += blockCount;}
      }
    }

    errs() << F.getName() << ", ";
    errs() << int(DynOpCounts) << ", ";
    if (DynOpCounts == 0) {
      errs() << format("%f", 0.0f);
    } 
    else { 
      errs() << format("%f, ", (IAlu/DynOpCounts));
      errs() << format("%f, ",(FAlu/DynOpCounts));
      errs() << format("%f, ",(Mem/DynOpCounts));
      errs() << format("%f, ",(BBranch/DynOpCounts));
      errs() << format("%f, ",(UBranch/DynOpCounts));
      errs() << format("%f",(Other/DynOpCounts)); 
    }

    errs() << "\n";
    
    return true;
  }
};
}

char Statics::ID = 0;
static RegisterPass<Statics> X("statistics", "Operation Statistics Pass",
                             false /* Only looks at CFG */,
                             false /* Analysis Pass */);