// SPDX-License-Identifier: MIT
/*
$info$
tags: ir|opts
desc: Puts guest memory behind GuestMemoryBase (32-bit system mode)
$end_info$
*/

#include "Interface/IR/IR.h"
#include "Interface/IR/IREmitter.h"
#include "Interface/IR/PassManager.h"

#include <FEXCore/Utils/Profiler.h>

namespace FEXCore::IR {

// Every guest memory access's address operand becomes AddGuestBase(address), which the
// backend lowers to a single `add xD, REG_MEMBASE, wA, uxtw`. Constant offsets the
// address-mode selection peeled off stay on the access and apply to the host address.
//
// Ops that return an updated guest address (Push, Pop, MemSet, MemCpy and their pairs)
// are left alone: the backend applies the base inside them, so the address they hand
// back stays a guest address.
class GuestMemoryBase final : public Pass {
public:
  void Run(IREmitter* IREmit) override;
};

void GuestMemoryBase::Run(IREmitter* IREmit) {
  FEXCORE_PROFILE_SCOPED("PassManager::GuestMemoryBase");

  auto CurrentIR = IREmit->ViewIR();

  for (auto [BlockNode, BlockHeader] : CurrentIR.GetBlocks()) {
    for (auto [CodeNode, IROp] : CurrentIR.GetCode(BlockNode)) {
      int Arg = -1;

      switch (IROp->Op) {
#define ADDR(Name, Field)     \
  case IROp_##Name::OPCODE:             \
    Arg = IROp_##Name::Field##_Index; \
    break;
        ADDR(LoadMem, Addr)
        ADDR(StoreMem, Addr)
        ADDR(LoadMemTSO, Addr)
        ADDR(StoreMemTSO, Addr)
        ADDR(LoadMemPair, Addr)
        ADDR(StoreMemPair, Addr)
        ADDR(LoadMemX87SVEOptPredicate, Addr)
        ADDR(StoreMemX87SVEOptPredicate, Addr)
        ADDR(VLoadVectorMasked, Addr)
        ADDR(VStoreVectorMasked, Addr)
        ADDR(VLoadVectorElement, Addr)
        ADDR(VStoreVectorElement, Addr)
        ADDR(VBroadcastFromMem, Address)
        ADDR(CacheLineClear, Addr)
        ADDR(CacheLineClean, Addr)
        ADDR(CacheLineZero, Addr)
        ADDR(Prefetch, Addr)
        ADDR(VStoreNonTemporal, Addr)
        ADDR(VStoreNonTemporalPair, Addr)
        ADDR(VLoadNonTemporal, Addr)
        ADDR(CAS, Addr)
        ADDR(CASPair, Addr)
        ADDR(AtomicSwap, Addr)
        ADDR(AtomicFetchAdd, Addr)
        ADDR(AtomicFetchSub, Addr)
        ADDR(AtomicFetchAnd, Addr)
        ADDR(AtomicFetchCLR, Addr)
        ADDR(AtomicFetchOr, Addr)
        ADDR(AtomicFetchXor, Addr)
        ADDR(AtomicFetchNeg, Addr)
#undef ADDR
      case IROp_VLoadVectorGatherMasked::OPCODE:
      case IROp_VLoadVectorGatherMaskedQPS::OPCODE:
        // AVX2 only: no 32-bit system-mode guest has it (the CPUID it sees has no AVX).
        LOGMAN_MSG_A_FMT("Gather with a guest memory base");
        break;
      default: break;
      }

      if (Arg < 0) {
        continue;
      }

      Ref Addr = CurrentIR.GetNode(IROp->Args[Arg]);
      const auto AddrOp = CurrentIR.GetOp<IROp_Header>(Addr)->Op;
      if (AddrOp == OP_LOADCONTEXTINDEXED || AddrOp == OP_FORMCONTEXTADDRESS) {
        // Host memory: a table the context points to (the GDT/LDT a segment load
        // reads) or the context itself (the x87 stack), not guest memory.
        continue;
      }
      IREmit->SetWriteCursorBefore(CodeNode);
      Ref Host = IREmit->_AddGuestBase(Addr);
      IREmit->ReplaceNodeArgument(CodeNode, Arg, Host);
    }
  }
}

fextl::unique_ptr<Pass> CreateGuestMemoryBasePass() {
  return fextl::make_unique<GuestMemoryBase>();
}

} // namespace FEXCore::IR
