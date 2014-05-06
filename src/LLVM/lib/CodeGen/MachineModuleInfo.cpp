//===-- llvm/CodeGen/MachineModuleInfo.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineModuleInfo.h"

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Module.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/Dwarf.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;
using namespace llvm::dwarf;

// Handle the Pass registration stuff necessary to use TargetData's.
INITIALIZE_PASS(MachineModuleInfo, "machinemoduleinfo",
                "Machine Module Information", false, false);
char MachineModuleInfo::ID = 0;

// Out of line virtual method.
MachineModuleInfoImpl::~MachineModuleInfoImpl() {}

namespace llvm {
class MMIAddrLabelMapCallbackPtr : CallbackVH {
  MMIAddrLabelMap *Map;
public:
  MMIAddrLabelMapCallbackPtr() : Map(0) {}
  MMIAddrLabelMapCallbackPtr(Value *V) : CallbackVH(V), Map(0) {}
  
  void setPtr(BasicBlock *BB) {
    ValueHandleBase::operator=(BB);
  }
    
  void setMap(MMIAddrLabelMap *map) { Map = map; }
  
  virtual void deleted();
  virtual void allUsesReplacedWith(Value *V2);
};
  
class MMIAddrLabelMap {
  MCContext &Context;
  struct AddrLabelSymEntry {
    /// Symbols - The symbols for the label.  This is a pointer union that is
    /// either one symbol (the common case) or a list of symbols.
    PointerUnion<MCSymbol *, std::vector<MCSymbol*>*> Symbols;
    
    Function *Fn;   // The containing function of the BasicBlock.
    unsigned Index; // The index in BBCallbacks for the BasicBlock.
  };
  
  DenseMap<AssertingVH<BasicBlock>, AddrLabelSymEntry> AddrLabelSymbols;
  
  /// BBCallbacks - Callbacks for the BasicBlock's that we have entries for.  We
  /// use this so we get notified if a block is deleted or RAUWd.
  std::vector<MMIAddrLabelMapCallbackPtr> BBCallbacks;

  /// DeletedAddrLabelsNeedingEmission - This is a per-function list of symbols
  /// whose corresponding BasicBlock got deleted.  These symbols need to be
  /// emitted at some point in the file, so AsmPrinter emits them after the
  /// function body.
  DenseMap<AssertingVH<Function>, std::vector<MCSymbol*> >
    DeletedAddrLabelsNeedingEmission;
public:
  
  MMIAddrLabelMap(MCContext &context) : Context(context) {}
  ~MMIAddrLabelMap() {
    assert(DeletedAddrLabelsNeedingEmission.empty() &&
           "Some labels for deleted blocks never got emitted");
    
    // Deallocate any of the 'list of symbols' case.
    for (DenseMap<AssertingVH<BasicBlock>, AddrLabelSymEntry>::iterator
         I = AddrLabelSymbols.begin(), E = AddrLabelSymbols.end(); I != E; ++I)
      if (I->second.Symbols.is<std::vector<MCSymbol*>*>())
        delete I->second.Symbols.get<std::vector<MCSymbol*>*>();
  }
  
  MCSymbol *getAddrLabelSymbol(BasicBlock *BB);
  std::vector<MCSymbol*> getAddrLabelSymbolToEmit(BasicBlock *BB);

  void takeDeletedSymbolsForFunction(Function *F, 
                                     std::vector<MCSymbol*> &Result);

  void UpdateForDeletedBlock(BasicBlock *BB);
  void UpdateForRAUWBlock(BasicBlock *Old, BasicBlock *New);
};
}

MCSymbol *MMIAddrLabelMap::getAddrLabelSymbol(BasicBlock *BB) {
  assert(BB->hasAddressTaken() &&
         "Shouldn't get label for block without address taken");
  AddrLabelSymEntry &Entry = AddrLabelSymbols[BB];
  
  // If we already had an entry for this block, just return it.
  if (!Entry.Symbols.isNull()) {
    assert(BB->getParent() == Entry.Fn && "Parent changed");
    if (Entry.Symbols.is<MCSymbol*>())
      return Entry.Symbols.get<MCSymbol*>();
    return (*Entry.Symbols.get<std::vector<MCSymbol*>*>())[0];
  }
  
  // Otherwise, this is a new entry, create a new symbol for it and add an
  // entry to BBCallbacks so we can be notified if the BB is deleted or RAUWd.
  BBCallbacks.push_back(BB);
  BBCallbacks.back().setMap(this);
  Entry.Index = BBCallbacks.size()-1;
  Entry.Fn = BB->getParent();
  MCSymbol *Result = Context.CreateTempSymbol();
  Entry.Symbols = Result;
  return Result;
}

std::vector<MCSymbol*>
MMIAddrLabelMap::getAddrLabelSymbolToEmit(BasicBlock *BB) {
  assert(BB->hasAddressTaken() &&
         "Shouldn't get label for block without address taken");
  AddrLabelSymEntry &Entry = AddrLabelSymbols[BB];
  
  std::vector<MCSymbol*> Result;
  
  // If we already had an entry for this block, just return it.
  if (Entry.Symbols.isNull())
    Result.push_back(getAddrLabelSymbol(BB));
  else if (MCSymbol *Sym = Entry.Symbols.dyn_cast<MCSymbol*>())
    Result.push_back(Sym);
  else
    Result = *Entry.Symbols.get<std::vector<MCSymbol*>*>();
  return Result;
}


/// takeDeletedSymbolsForFunction - If we have any deleted symbols for F, return
/// them.
void MMIAddrLabelMap::
takeDeletedSymbolsForFunction(Function *F, std::vector<MCSymbol*> &Result) {
  DenseMap<AssertingVH<Function>, std::vector<MCSymbol*> >::iterator I =
    DeletedAddrLabelsNeedingEmission.find(F);

  // If there are no entries for the function, just return.
  if (I == DeletedAddrLabelsNeedingEmission.end()) return;
  
  // Otherwise, take the list.
  std::swap(Result, I->second);
  DeletedAddrLabelsNeedingEmission.erase(I);
}


void MMIAddrLabelMap::UpdateForDeletedBlock(BasicBlock *BB) {
  // If the block got deleted, there is no need for the symbol.  If the symbol
  // was already emitted, we can just forget about it, otherwise we need to
  // queue it up for later emission when the function is output.
  AddrLabelSymEntry Entry = AddrLabelSymbols[BB];
  AddrLabelSymbols.erase(BB);
  assert(!Entry.Symbols.isNull() && "Didn't have a symbol, why a callback?");
  BBCallbacks[Entry.Index] = 0;  // Clear the callback.

  assert((BB->getParent() == 0 || BB->getParent() == Entry.Fn) &&
         "Block/parent mismatch");

  // Handle both the single and the multiple symbols cases.
  if (MCSymbol *Sym = Entry.Symbols.dyn_cast<MCSymbol*>()) {
    if (Sym->isDefined())
      return;
  
    // If the block is not yet defined, we need to emit it at the end of the
    // function.  Add the symbol to the DeletedAddrLabelsNeedingEmission list
    // for the containing Function.  Since the block is being deleted, its
    // parent may already be removed, we have to get the function from 'Entry'.
    DeletedAddrLabelsNeedingEmission[Entry.Fn].push_back(Sym);
  } else {
    std::vector<MCSymbol*> *Syms = Entry.Symbols.get<std::vector<MCSymbol*>*>();

    for (unsigned i = 0, e = Syms->size(); i != e; ++i) {
      MCSymbol *Sym = (*Syms)[i];
      if (Sym->isDefined()) continue;  // Ignore already emitted labels.
      
      // If the block is not yet defined, we need to emit it at the end of the
      // function.  Add the symbol to the DeletedAddrLabelsNeedingEmission list
      // for the containing Function.  Since the block is being deleted, its
      // parent may already be removed, we have to get the function from
      // 'Entry'.
      DeletedAddrLabelsNeedingEmission[Entry.Fn].push_back(Sym);
    }
    
    // The entry is deleted, free the memory associated with the symbol list.
    delete Syms;
  }
}

void MMIAddrLabelMap::UpdateForRAUWBlock(BasicBlock *Old, BasicBlock *New) {
  // Get the entry for the RAUW'd block and remove it from our map.
  AddrLabelSymEntry OldEntry = AddrLabelSymbols[Old];
  AddrLabelSymbols.erase(Old);
  assert(!OldEntry.Symbols.isNull() && "Didn't have a symbol, why a callback?");

  AddrLabelSymEntry &NewEntry = AddrLabelSymbols[New];

  // If New is not address taken, just move our symbol over to it.
  if (NewEntry.Symbols.isNull()) {
    BBCallbacks[OldEntry.Index].setPtr(New);    // Update the callback.
    NewEntry = OldEntry;     // Set New's entry.
    return;
  }

  BBCallbacks[OldEntry.Index] = 0;    // Update the callback.

  // Otherwise, we need to add the old symbol to the new block's set.  If it is
  // just a single entry, upgrade it to a symbol list.
  if (MCSymbol *PrevSym = NewEntry.Symbols.dyn_cast<MCSymbol*>()) {
    std::vector<MCSymbol*> *SymList = new std::vector<MCSymbol*>();
    SymList->push_back(PrevSym);
    NewEntry.Symbols = SymList;
  }
      
  std::vector<MCSymbol*> *SymList =
    NewEntry.Symbols.get<std::vector<MCSymbol*>*>();

  // If the old entry was a single symbol, add it.
  if (MCSymbol *Sym = OldEntry.Symbols.dyn_cast<MCSymbol*>()) {
    SymList->push_back(Sym);
    return;
  }
  
  // Otherwise, concatenate the list.
  std::vector<MCSymbol*> *Syms =OldEntry.Symbols.get<std::vector<MCSymbol*>*>();
  SymList->insert(SymList->end(), Syms->begin(), Syms->end());
  delete Syms;
}


void MMIAddrLabelMapCallbackPtr::deleted() {
  Map->UpdateForDeletedBlock(cast<BasicBlock>(getValPtr()));
}

void MMIAddrLabelMapCallbackPtr::allUsesReplacedWith(Value *V2) {
  Map->UpdateForRAUWBlock(cast<BasicBlock>(getValPtr()), cast<BasicBlock>(V2));
}


//===----------------------------------------------------------------------===//

MachineModuleInfo::MachineModuleInfo(const MCAsmInfo &MAI)
: ImmutablePass(ID), Context(MAI),
  ObjFileMMI(0),
  CurCallSite(0), DbgInfoAvailable(false){
  AddrLabelSymbols = 0;
  TheModule = 0;
}

MachineModuleInfo::MachineModuleInfo()
: ImmutablePass(ID), Context(*(MCAsmInfo*)0) {
  assert(0 && "This MachineModuleInfo constructor should never be called, MMI "
         "should always be explicitly constructed by LLVMTargetMachine");
  abort();
}

MachineModuleInfo::~MachineModuleInfo() {
  delete ObjFileMMI;
  
  // FIXME: Why isn't doFinalization being called??
  //assert(AddrLabelSymbols == 0 && "doFinalization not called");
  delete AddrLabelSymbols;
  AddrLabelSymbols = 0;
}

/// doInitialization - Initialize the state for a new module.
///
bool MachineModuleInfo::doInitialization() {
  assert(AddrLabelSymbols == 0 && "Improperly initialized");
  return false;
}

/// doFinalization - Tear down the state after completion of a module.
///
bool MachineModuleInfo::doFinalization() {
  delete AddrLabelSymbols;
  AddrLabelSymbols = 0;
  return false;
}

/// EndFunction - Discard function meta information.
///
void MachineModuleInfo::EndFunction() {
  // Clean up frame info.
  FrameMoves.clear();

  // Clean up exception info.
  CallSiteMap.clear();
  TypeInfos.clear();
  FilterIds.clear();
  FilterEnds.clear();
  VariableDbgInfo.clear();
}

/// AnalyzeModule - Scan the module for global debug information.
///
void MachineModuleInfo::AnalyzeModule(const Module &M) {
  // Insert functions in the llvm.used array (but not llvm.compiler.used) into
  // UsedFunctions.
  const GlobalVariable *GV = M.getGlobalVariable("llvm.used");
  if (!GV || !GV->hasInitializer()) return;

  // Should be an array of 'i8*'.
  const ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
  if (InitList == 0) return;

  for (unsigned i = 0, e = InitList->getNumOperands(); i != e; ++i)
    if (const Function *F =
          dyn_cast<Function>(InitList->getOperand(i)->stripPointerCasts()))
      UsedFunctions.insert(F);
}

//===- Address of Block Management ----------------------------------------===//


/// getAddrLabelSymbol - Return the symbol to be used for the specified basic
/// block when its address is taken.  This cannot be its normal LBB label
/// because the block may be accessed outside its containing function.
MCSymbol *MachineModuleInfo::getAddrLabelSymbol(const BasicBlock *BB) {
  // Lazily create AddrLabelSymbols.
  if (AddrLabelSymbols == 0)
    AddrLabelSymbols = new MMIAddrLabelMap(Context);
  return AddrLabelSymbols->getAddrLabelSymbol(const_cast<BasicBlock*>(BB));
}

/// getAddrLabelSymbolToEmit - Return the symbol to be used for the specified
/// basic block when its address is taken.  If other blocks were RAUW'd to
/// this one, we may have to emit them as well, return the whole set.
std::vector<MCSymbol*> MachineModuleInfo::
getAddrLabelSymbolToEmit(const BasicBlock *BB) {
  // Lazily create AddrLabelSymbols.
  if (AddrLabelSymbols == 0)
    AddrLabelSymbols = new MMIAddrLabelMap(Context);
 return AddrLabelSymbols->getAddrLabelSymbolToEmit(const_cast<BasicBlock*>(BB));
}


/// takeDeletedSymbolsForFunction - If the specified function has had any
/// references to address-taken blocks generated, but the block got deleted,
/// return the symbol now so we can emit it.  This prevents emitting a
/// reference to a symbol that has no definition.
void MachineModuleInfo::
takeDeletedSymbolsForFunction(const Function *F,
                              std::vector<MCSymbol*> &Result) {
  // If no blocks have had their addresses taken, we're done.
  if (AddrLabelSymbols == 0) return;
  return AddrLabelSymbols->
     takeDeletedSymbolsForFunction(const_cast<Function*>(F), Result);
}

/// getTypeIDFor - Return the type id for the specified typeinfo.  This is
/// function wide.
unsigned MachineModuleInfo::getTypeIDFor(const GlobalVariable *TI) {
  for (unsigned i = 0, N = TypeInfos.size(); i != N; ++i)
    if (TypeInfos[i] == TI) return i + 1;

  TypeInfos.push_back(TI);
  return TypeInfos.size();
}

/// getFilterIDFor - Return the filter id for the specified typeinfos.  This is
/// function wide.
int MachineModuleInfo::getFilterIDFor(std::vector<unsigned> &TyIds) {
  // If the new filter coincides with the tail of an existing filter, then
  // re-use the existing filter.  Folding filters more than this requires
  // re-ordering filters and/or their elements - probably not worth it.
  for (std::vector<unsigned>::iterator I = FilterEnds.begin(),
       E = FilterEnds.end(); I != E; ++I) {
    unsigned i = *I, j = TyIds.size();

    while (i && j)
      if (FilterIds[--i] != TyIds[--j])
        goto try_next;

    if (!j)
      // The new filter coincides with range [i, end) of the existing filter.
      return -(1 + i);

try_next:;
  }

  // Add the new filter.
  int FilterID = -(1 + FilterIds.size());
  FilterIds.reserve(FilterIds.size() + TyIds.size() + 1);
  for (unsigned I = 0, N = TyIds.size(); I != N; ++I)
    FilterIds.push_back(TyIds[I]);
  FilterEnds.push_back(FilterIds.size());
  FilterIds.push_back(0); // terminator
  return FilterID;
}

namespace {
  /// VariableDebugSorter - Comparison to sort the VariableDbgInfo map
  /// by source location, to avoid depending on the arbitrary order that
  /// instruction selection visits variables in.
  struct VariableDebugSorter {
    bool operator()(const MachineModuleInfo::VariableDbgInfoMapTy::value_type &A,
                    const MachineModuleInfo::VariableDbgInfoMapTy::value_type &B)
                  const {
       if (A.second.second.getLine() != B.second.second.getLine())
         return A.second.second.getLine() < B.second.second.getLine();
       if (A.second.second.getCol() != B.second.second.getCol())
         return A.second.second.getCol() < B.second.second.getCol();
       return false;
    }
  };
}