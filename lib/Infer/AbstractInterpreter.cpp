// Copyright 2019 The Souper Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "souper/Extractor/Solver.h"
#include "souper/Infer/Interpreter.h"
#include "souper/Infer/AbstractInterpreter.h"

using namespace llvm;

namespace {

  APInt getUMin(const KnownBits &x) { return x.One; }

  APInt getUMax(const KnownBits &x) { return ~x.Zero; }

  bool isSignKnown(const KnownBits &x) {
    unsigned W = x.getBitWidth();
    return x.One[W - 1] || x.Zero[W - 1];
  }

  APInt getSMin(const KnownBits &x) {
    if (isSignKnown(x))
      return x.One;
    APInt Min = x.One;
    Min.setBit(x.getBitWidth() - 1);
    return Min;
  }

  APInt getSMax(const KnownBits &x) {
    if (isSignKnown(x))
      return ~x.Zero;
    APInt Max = ~x.Zero;
    Max.clearBit(x.getBitWidth() - 1);
    return Max;
  }


} // anonymous

namespace souper {

  KnownBits KnownBitsAnalysis::getMostPreciseKnownBits(KnownBits a, KnownBits b) {
    unsigned unknownCountA =
      a.getBitWidth() - (a.Zero.countPopulation() + a.One.countPopulation());
    unsigned unknownCountB =
      b.getBitWidth() - (b.Zero.countPopulation() + b.One.countPopulation());
    return unknownCountA < unknownCountB ? a : b;
  }

  std::string KnownBitsAnalysis::knownBitsString(llvm::KnownBits KB) {
    std::string S = "";
    for (int I = 0; I < KB.getBitWidth(); I++) {
      if (KB.Zero.isNegative())
        S += "0";
      else if (KB.One.isNegative())
        S += "1";
      else
        S += "?";
      KB.Zero <<= 1;
      KB.One <<= 1;
    }
    return S;
  }

  namespace BinaryTransferFunctionsKB {
    llvm::KnownBits add(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      return llvm::KnownBits::computeForAddSub(/*Add=*/true, /*NSW=*/false,
                                               lhs, rhs);
    }

    llvm::KnownBits addnsw(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      return llvm::KnownBits::computeForAddSub(/*Add=*/true, /*NSW=*/true,
                                               lhs, rhs);
    }

    llvm::KnownBits sub(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      return llvm::KnownBits::computeForAddSub(/*Add=*/false, /*NSW=*/false,
                                               lhs, rhs);
    }

    llvm::KnownBits subnsw(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      return llvm::KnownBits::computeForAddSub(/*Add=*/false, /*NSW=*/true,
                                               lhs, rhs);
    }

    llvm::KnownBits mul(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(lhs.getBitWidth());

      // TODO: Below only takes into account leading and trailing zeros. Maybe
      // also do something with leading ones or trailing ones for improvement?
      auto trailingZeros0 = lhs.countMinTrailingZeros();
      auto trailingZeros1 = rhs.countMinTrailingZeros();
      Result.Zero.setLowBits(std::min(trailingZeros0 + trailingZeros1, lhs.getBitWidth()));

      // check for leading zeros
      auto lz0 = lhs.countMinLeadingZeros();
      auto lz1 = rhs.countMinLeadingZeros();
      auto confirmedLeadingZeros = lz0 + lz1 - 1;
      auto resultSize = lhs.getBitWidth() + rhs.getBitWidth() - 1;
      if (resultSize - confirmedLeadingZeros < lhs.getBitWidth())
        Result.Zero.setHighBits(lhs.getBitWidth() - (resultSize - confirmedLeadingZeros));

      // two numbers odd means reuslt is odd
      if (lhs.One[0] && rhs.One[0])
        Result.One.setLowBits(1);

      return Result;
    }

    llvm::KnownBits udiv(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(lhs.getBitWidth());
      const auto width = Result.getBitWidth();

      unsigned LeadZ = lhs.countMinLeadingZeros();
      unsigned RHSMaxLeadingZeros = rhs.countMaxLeadingZeros();
      if (RHSMaxLeadingZeros != width)
        LeadZ = std::min(width, LeadZ + width - RHSMaxLeadingZeros - 1);
      Result.Zero.setHighBits(LeadZ);
      return Result;
    }

    llvm::KnownBits urem(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(lhs.getBitWidth());
      const auto width = Result.getBitWidth();

      if (rhs.isConstant()) {
        auto RA = rhs.getConstant();
        if (RA.isPowerOf2()) {
          auto LowBits = (RA - 1);
          Result = lhs;
          Result.Zero |= ~LowBits;
          Result.One &= LowBits;
          return Result;
        }
      }

      unsigned Leaders =
        std::max(lhs.countMinLeadingZeros(), rhs.countMinLeadingZeros());
      Result.resetAll();
      Result.Zero.setHighBits(Leaders);
      return Result;
    }

    llvm::KnownBits and_(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      auto result = lhs;
      result.One &= rhs.One;
      result.Zero |= rhs.Zero;
      return result;
    }

    llvm::KnownBits or_(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      auto result = lhs;
      result.One |= rhs.One;
      result.Zero &= rhs.Zero;
      return result;
    }

    llvm::KnownBits xor_(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      auto result = lhs;
      llvm::APInt KnownZeroOut =
        (lhs.Zero & rhs.Zero) | (lhs.One & rhs.One);
      result.One = (lhs.Zero & rhs.One) | (lhs.One & rhs.Zero);
      result.Zero = std::move(KnownZeroOut);
      // ^ logic copied from LLVM ValueTracking.cpp
      return result;
    }

    llvm::KnownBits shl(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(lhs.getBitWidth());
      const auto width = Result.getBitWidth();

      auto Op0KB = lhs;
      if (rhs.isConstant()) {
        auto Val = rhs.getConstant().getLimitedValue();
        if (Val < 0 || Val >= width) {
          return Result;
        }

        Op0KB.One <<= Val;
        Op0KB.Zero <<= Val;
        Op0KB.Zero.setLowBits(Val);
        // setLowBits takes an unsigned int, so getLimitedValue is harmless
        return Op0KB;
      }

      unsigned minValue = rhs.One.getLimitedValue();
      Result.Zero.setLowBits(std::min(lhs.countMinTrailingZeros() + minValue, width));

      return Result;
    }

    llvm::KnownBits lshr(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(lhs.getBitWidth());
      const auto width = Result.getBitWidth();

      auto Op0KB = lhs;
      if (rhs.isConstant()) {
        auto Val = rhs.getConstant().getLimitedValue();
        if (Val < 0 || Val >= width) {
          return Result;
        }
        Op0KB.One.lshrInPlace(Val);
        Op0KB.Zero.lshrInPlace(Val);
        Op0KB.Zero.setHighBits(Val);
        // setHighBits takes an unsigned int, so getLimitedValue is harmless
        return Op0KB;
      }

      unsigned minValue = rhs.One.getLimitedValue();
      Result.Zero.setHighBits(std::min(minValue + lhs.countMinLeadingZeros(), width));

      return Result;
    }

    llvm::KnownBits ashr(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(lhs.getBitWidth());
      const auto width = Result.getBitWidth();

      unsigned minValue = rhs.One.getLimitedValue();
      if (lhs.One.isSignBitSet()) {
        // confirmed: sign bit = 1
        Result.One.setHighBits(std::min(lhs.countMinLeadingOnes() + minValue, width));
      } else if (lhs.Zero.isSignBitSet()) {
        // confirmed: sign bit = 0
        Result.Zero.setHighBits(std::min(lhs.countMinLeadingZeros() + minValue, width));
      }
      return Result;
    }

    llvm::KnownBits eq(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(1);
      if (lhs.isConstant() && rhs.isConstant() && (lhs.getConstant() == rhs.getConstant())) {
        Result.One.setBit(0);
        return Result;
      }
      if (((lhs.One & rhs.Zero) != 0) || ((lhs.Zero & rhs.One) != 0)) {
        Result.Zero.setBit(0);
        return Result;
      }
      return Result;
    }

    llvm::KnownBits ne(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(1);

      if (lhs.isConstant() && rhs.isConstant() && (lhs.getConstant() == rhs.getConstant()))
        Result.Zero.setBit(0);
      if (((lhs.One & rhs.Zero) != 0) || ((lhs.Zero & rhs.One) != 0))
        Result.One.setBit(0);
      return Result;
    }

    llvm::KnownBits ult(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(1);
      if (getUMax(lhs).ult(getUMin(rhs)))
        Result.One.setBit(0);
      if (getUMin(lhs).uge(getUMax(rhs)))
        Result.Zero.setBit(0);
      return Result;
    }

    llvm::KnownBits slt(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(1);
      if (getSMax(lhs).slt(getSMin(rhs)))
        Result.One.setBit(0);
      if (getSMin(lhs).sge(getSMax(rhs)))
        Result.Zero.setBit(0);
      return Result;
    }

    llvm::KnownBits ule(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(1);
      if (getUMax(lhs).ule(getUMin(rhs)))
        Result.One.setBit(0);
      if (getUMin(lhs).ugt(getUMax(rhs)))
        Result.Zero.setBit(0);
      return Result;
    }

    llvm::KnownBits sle(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(1);
      if (getSMax(lhs).sle(getSMin(rhs)))
        Result.One.setBit(0);
      if (getSMin(lhs).sgt(getSMax(rhs)))
        Result.Zero.setBit(0);
      return Result;
    }
  }

  bool isConcrete(Inst *I, bool ConsiderConsts, bool ConsiderHoles) {
    return !hasGivenInst(I, [ConsiderConsts, ConsiderHoles](Inst* instr) {
      if (ConsiderConsts && isReservedConst(instr))
        return true;
      if (ConsiderHoles && isReservedInst(instr))
        return true;
      return false;
    });
  }

  // Tries to get the concrete value from @I. If PartialEval is enabled, it tries to
  // launch concrete interpreter to get the value. If not possible, it returns
  // EvalValue of ValueKind::Unimplemented
  EvalValue getValue(Inst *I, ConcreteInterpreter &CI, bool PartialEval) {
    if (I->K == Inst::Const)
      return {I->Val};
    else if (I->K == Inst::Var && !isReservedConst(I))
      // evaluateInst will only give us input value of the variable; it doesn't
      // evaluate anything.
      return CI.evaluateInst(I);

    if (PartialEval && isConcrete(I))
      return CI.evaluateInst(I);

    // unimplemented
    return EvalValue();
  }

#define KB0 findKnownBits(I->Ops[0], CI, PartialEval)
#define KB1 findKnownBits(I->Ops[1], CI, PartialEval)
#define KB2 findKnownBits(I->Ops[2], CI, PartialEval)
#define VAL(INST) getValue(INST, CI, PartialEval)

  llvm::KnownBits mergeKnownBits(std::vector<llvm::KnownBits> vec) {
    assert(vec.size() > 0);

    auto width = vec[0].getBitWidth();
    for (unsigned i = 1; i < vec.size(); i++) {
      if (width != vec[i].getBitWidth())
        llvm::report_fatal_error("mergeKnownBits: bitwidth should be same of all inputs");
    }

    KnownBits Result(width);
    for (unsigned i = 0; i < width; i++) {
      bool one = vec[0].One[i];
      bool zero = vec[0].Zero[i];
      for (unsigned j = 1; j < vec.size(); j++) {
        if (!one || !vec[j].One[i])
          one = false;
        if (!zero || !vec[j].Zero[i])
          zero = false;
      }
      if (one)
        Result.One.setBit(i);
      if (zero)
        Result.Zero.setBit(i);
    }
    return Result;
  }

  bool KnownBitsAnalysis::cacheHasValue(Inst* I) {
    if (KBCache.find(I) != KBCache.end())
      return true;

    if (I->K == Inst::Var && (I->KnownZeros.getBoolValue() || I->KnownOnes.getBoolValue())) {
      llvm::KnownBits metadataKB;
      metadataKB.Zero = I->KnownZeros;
      metadataKB.One = I->KnownOnes;

      KBCache.emplace(I, std::move(metadataKB));
      return true;
    }

    return false;
  }

  llvm::KnownBits KnownBitsAnalysis::findKnownBits(Inst *I, ConcreteInterpreter &CI, bool PartialEval) {
    llvm::KnownBits Result(I->Width);

    if (cacheHasValue(I))
      return KBCache.at(I);

    EvalValue V = VAL(I);
    if (V.hasValue()) {
      Result.One = V.getValue();
      Result.Zero = ~V.getValue();

      // cache before returning
      KBCache.emplace(I, Result);

      return Result;
    }

    switch(I->K) {
    case Inst::Phi: {
      std::vector<llvm::KnownBits> vec;
      for (auto &Op : I->Ops) {
        vec.emplace_back(findKnownBits(Op, CI, PartialEval));
      }
      Result = mergeKnownBits(vec);
    }
    case Inst::AddNUW :
    case Inst::AddNW :
    case Inst::Add:
      Result = BinaryTransferFunctionsKB::add(KB0, KB1);
      break;
    case Inst::AddNSW:
      Result = BinaryTransferFunctionsKB::addnsw(KB0, KB1);
      break;
    case Inst::SubNUW :
    case Inst::SubNW :
    case Inst::Sub:
      Result = BinaryTransferFunctionsKB::sub(KB0, KB1);
      break;
    case Inst::SubNSW:
      Result = BinaryTransferFunctionsKB::subnsw(KB0, KB1);
      break;
    case Inst::Mul:
      Result = BinaryTransferFunctionsKB::mul(KB0, KB1);
      break;
//   case MulNSW:
//     return "mulnsw";
//   case MulNUW:
//     return "mulnuw";
//   case MulNW:
//     return "mulnw";
    case Inst::UDiv:
      Result = BinaryTransferFunctionsKB::udiv(KB0, KB1);
      break;
//   case SDiv:
//     return "sdiv";
//   case UDivExact:
//     return "udivexact";
//   case SDivExact:
//     return "sdivexact";
    case Inst::URem:
      Result = BinaryTransferFunctionsKB::urem(KB0, KB1);
      break;
//   case SRem:
//     return "srem";
    case Inst::And :
      Result = BinaryTransferFunctionsKB::and_(KB0, KB1);
      break;
    case Inst::Or :
      Result = BinaryTransferFunctionsKB::or_(KB0, KB1);
      break;
    case Inst::Xor :
      Result = BinaryTransferFunctionsKB::xor_(KB0, KB1);
      break;
    case Inst::ShlNSW :
    case Inst::ShlNUW :
    case Inst::ShlNW : // TODO: Rethink if these make sense
    case Inst::Shl : {
      // we can't easily put following condition inside
      // BinaryTransferFunctionsKB but this one gives significant pruning; so,
      // let's keep it here.
      // Note that only code inside BinaryTransferFunctionsKB is testable from
      // unit tests. Put minimum code outside it which you are sure of being
      // correct.
      if (isReservedConst(I->Ops[1]))
        Result.Zero.setLowBits(1);
      Result = getMostPreciseKnownBits(Result, BinaryTransferFunctionsKB::shl(KB0, KB1));
      break;
    }
    case Inst::LShr : {
      if (isReservedConst(I->Ops[1]))
        Result.Zero.setHighBits(1);
      Result = getMostPreciseKnownBits(Result, BinaryTransferFunctionsKB::lshr(KB0, KB1));
      break;
    }
//   case LShrExact:
//     return "lshrexact";
    case Inst::AShr:
      if (isReservedConst(I->Ops[1])) {
        if (KB0.Zero[KB0.getBitWidth() - 1])
          Result.Zero.setHighBits(2);
        if (KB0.One[KB0.getBitWidth() - 1])
          Result.One.setHighBits(2);
      }

      Result = getMostPreciseKnownBits(Result, BinaryTransferFunctionsKB::ashr(KB0, KB1));
      break;
//   case AShrExact:
//     return "ashrexact";
    case Inst::Select:
      if (KB0.One.getBoolValue())
        Result = KB1;
      else if (KB0.Zero.getBoolValue())
        Result = KB2;
      else
        Result = mergeKnownBits({KB1, KB2});
      break;
    case Inst::ZExt: {
      // below code copied from LLVM master. Directly use KnownBits::zext() when
      // we move to LLVM9
      unsigned OldBitWidth = KB0.getBitWidth();
      APInt NewZero = KB0.Zero.zext(I->Width);
      NewZero.setBitsFrom(OldBitWidth);
      Result.Zero = NewZero;
      Result.One = KB0.One.zext(I->Width);
      break;
    }
    case Inst::SExt:
      Result = KB0.sext(I->Width);
      break;
    case Inst::Trunc:
      Result = KB0.trunc(I->Width);
      break;
    case Inst::Eq: {
      // Below implementation, because it contains isReservedConst, is
      // difficult to put inside BinaryTransferFunctionsKB but it's able to
      // prune more stuff; so, let's keep both
      Inst *Constant = nullptr;
      llvm::KnownBits Other;
      if (isReservedConst(I->Ops[0])) {
        Constant = I->Ops[0];
        Other = KB1;
      } else if (isReservedConst(I->Ops[1])) {
        Constant = I->Ops[1];
        Other = KB0;
      }

      // Constants are never equal to 0
      if (Constant != nullptr && Other.Zero.isAllOnesValue()) {
        Result.Zero.setBit(0);
      }

      // Fallback to our tested implmentation
      Result = getMostPreciseKnownBits(Result, BinaryTransferFunctionsKB::eq(KB0, KB1));
      break;
    }
    case Inst::Ne:
      Result = BinaryTransferFunctionsKB::ne(KB0, KB1);
      break;
    case Inst::Ult:
      Result = BinaryTransferFunctionsKB::ult(KB0, KB1);
      break;
    case Inst::Slt:
      Result = BinaryTransferFunctionsKB::slt(KB0, KB1);
      break;
    case Inst::Ule:
      Result = BinaryTransferFunctionsKB::ule(KB0, KB1);
      break;
    case Inst::Sle:
      Result = BinaryTransferFunctionsKB::sle(KB0, KB1);
      break;
    case Inst::CtPop: {
      APInt val(KB0.getBitWidth(), KB0.countMaxPopulation());
      Result.Zero.setHighBits(KB0.getBitWidth() - val.getActiveBits());
      break;
    }
    case Inst::BSwap: {
      Result = KB0;
      Result.One = Result.One.byteSwap();
      Result.Zero = Result.Zero.byteSwap();
      break;
    }
    case Inst::BitReverse: {
      Result = KB0;
      Result.One = Result.One.reverseBits();
      Result.Zero = Result.Zero.reverseBits();
      break;
    }
    case Inst::Cttz: {
      APInt val(KB0.getBitWidth(), KB0.countMaxTrailingZeros());
      Result.Zero.setHighBits(KB0.getBitWidth() - val.getActiveBits());
      break;
    }
    case Inst::Ctlz: {
      APInt val(KB0.getBitWidth(), KB0.countMaxLeadingZeros());
      Result.Zero.setHighBits(KB0.getBitWidth() - val.getActiveBits());
      break;
    }
//   case FShl:
//     return "fshl";
//   case FShr:
//     return "fshr";
//   case ExtractValue:
//     return "extractvalue";
//   case SAddWithOverflow:
//     return "sadd.with.overflow";
//   case UAddWithOverflow:
//     return "uadd.with.overflow";
//   case SSubWithOverflow:
//     return "ssub.with.overflow";
//   case USubWithOverflow:
//     return "usub.with.overflow";
//   case SMulWithOverflow:
//     return "smul.with.overflow";
//   case UMulWithOverflow:
//     return "umul.with.overflow";
//   case ReservedConst:
//     return "reservedconst";
//   case ReservedInst:
//     return "reservedinst";
//   case SAddO:
//   case UAddO:
//   case SSubO:
//   case USubO:
//   case SMulO:
//   case UMulO:
    default :
      break;
    }

    assert(!Result.hasConflict() && "Conflict in resulting KB!");

    KBCache.emplace(I, Result);
    return KBCache.at(I);
  }

#undef KB0
#undef KB1
#undef KB2

  llvm::KnownBits KnownBitsAnalysis::findKnownBitsUsingSolver(Inst *I,
                                                              Solver *S,
                                                              std::vector<InstMapping> &PCs) {
    BlockPCs BPCs;
    InstContext IC;
    return S->findKnownBitsUsingSolver(BPCs, PCs, I, IC);
  }

#define CR0 findConstantRange(I->Ops[0], CI, PartialEval)
#define CR1 findConstantRange(I->Ops[1], CI, PartialEval)
#define CR2 findConstantRange(I->Ops[2], CI, PartialEval)

  bool ConstantRangeAnalysis::cacheHasValue(Inst* I) {
    if (CRCache.find(I) != CRCache.end())
      return true;

    if (I->K == Inst::Var && !I->Range.isFullSet()) {
      CRCache.emplace(I, I->Range);
      return true;
    }

    return false;
  }

  llvm::ConstantRange ConstantRangeAnalysis::findConstantRange(Inst *I,
                                                               ConcreteInterpreter &CI,
                                                               bool PartialEval) {
    llvm::ConstantRange Result(I->Width);

    if (cacheHasValue(I))
      return CRCache.at(I);

    EvalValue V = VAL(I);
    if (V.hasValue()) {
      CRCache.emplace(I, llvm::ConstantRange(V.getValue()));
      return CRCache.at(I);
    }

    switch (I->K) {
    case Inst::Const:
    case Inst::Var :
      if (isReservedConst(I))
        Result = llvm::ConstantRange(llvm::APInt(I->Width, 0)).inverse();
      break;
    case Inst::Trunc:
      Result = CR0.truncate(I->Width);
      break;
    case Inst::SExt:
      Result = CR0.signExtend(I->Width);
      break;
    case Inst::ZExt:
      Result = CR0.zeroExtend(I->Width);
      break;
    case souper::Inst::AddNUW :
    case souper::Inst::AddNW : // TODO: Rethink if these make sense
    case Inst::Add:
      Result = CR0.add(CR1);
      break;
    case Inst::AddNSW: {
      auto V1 = VAL(I->Ops[1]);
      if (V1.hasValue()) {
        Result = CR0.addWithNoSignedWrap(V1.getValue());
      }
      break;
    }
    case souper::Inst::SubNSW :
    case souper::Inst::SubNUW :
    case souper::Inst::SubNW : // TODO: Rethink if these make sense
    case Inst::Sub:
      Result = CR0.sub(CR1);
      break;
    case souper::Inst::MulNSW :
    case souper::Inst::MulNUW :
    case souper::Inst::MulNW : // TODO: Rethink if these make sense
    case Inst::Mul:
      Result = CR0.multiply(CR1);
      break;
    case Inst::And: {
      if (CR0.isEmptySet() || CR1.isEmptySet()) {
        Result = llvm::ConstantRange(CR0.getBitWidth(), /*isFullSet=*/false);
        break;
      }

      APInt umin = APIntOps::umin(CR1.getUnsignedMax(), CR0.getUnsignedMax());
      if (umin.isAllOnesValue()) {
        Result = llvm::ConstantRange(CR0.getBitWidth(), /*isFullSet=*/true);
        break;
      }

      APInt res = APInt::getNullValue(CR0.getBitWidth());

      const APInt upper1 = CR0.getUnsignedMax();
      const APInt upper2 = CR1.getUnsignedMax();
      APInt lower1 = CR0.getUnsignedMin();
      APInt lower2 = CR1.getUnsignedMin();
      const APInt tmp = lower1 & lower2;
      const unsigned bitPos = CR0.getBitWidth() - tmp.countLeadingZeros();
      // if there are no zeros from bitPos upto both barriers, lower bound have bit
      // set at bitPos. Barrier is the point beyond which you cannot set the bit
      // because it will be greater than the upper bound then
      if (!CR0.isWrappedSet() && !CR1.isWrappedSet() &&
          (lower1.countLeadingZeros() == upper1.countLeadingZeros()) &&
          (lower2.countLeadingZeros() == upper2.countLeadingZeros()) &&
          bitPos > 0) {
        lower1.lshrInPlace(bitPos - 1);
        lower2.lshrInPlace(bitPos - 1);
        if (lower1.countTrailingOnes() == (CR0.getBitWidth() - lower1.countLeadingZeros()) &&
            lower2.countTrailingOnes() == (CR0.getBitWidth() - lower2.countLeadingZeros())) {
          res = APInt::getOneBitSet(CR0.getBitWidth(), bitPos - 1);
        }
      }

      Result = llvm::ConstantRange(std::move(res), std::move(umin) + 1);
      break;
    }
    case Inst::Or: {
      if (CR0.isEmptySet() || CR1.isEmptySet()) {
        Result = llvm::ConstantRange(CR0.getBitWidth(), /*isFullSet=*/false);
        break;
      }

      APInt umax = APIntOps::umax(CR0.getUnsignedMin(), CR1.getUnsignedMin());
      APInt res = APInt::getNullValue(CR0.getBitWidth());
      if (!CR0.isWrappedSet() && !CR1.isWrappedSet()) {
        APInt umaxupper = APIntOps::umax(CR0.getUnsignedMax(), CR1.getUnsignedMax());
        APInt uminupper = APIntOps::umin(CR0.getUnsignedMax(), CR1.getUnsignedMax());
        res = APInt::getLowBitsSet(CR0.getBitWidth(),
                                   CR0.getBitWidth() - uminupper.countLeadingZeros());
        res = res | umaxupper;
        res = res + 1;
      }

      if (umax == res)
        Result = llvm::ConstantRange(CR0.getBitWidth(), /*isFullSet=*/true);
      else
        Result = llvm::ConstantRange(std::move(umax), std::move(res));
      break;
    }
    case souper::Inst::ShlNSW :
    case souper::Inst::ShlNUW :
    case souper::Inst::ShlNW : // TODO: Rethink if these make sense
    case Inst::Shl:
      Result = CR0.shl(CR1);
      break;
    case Inst::AShr:
      Result = CR0.ashr(CR1);
      break;
    case Inst::LShr:
      Result = CR0.lshr(CR1);
      break;
    case Inst::UDiv:
      Result = CR0.udiv(CR1);
      break;
    case Inst::Ctlz:
    case Inst::Cttz:
      Result = llvm::ConstantRange(llvm::APInt(I->Width, 0),
                                   llvm::APInt(I->Width, isReservedConst(I->Ops[0]) ?
                                               I->Ops[0]->Width :
                                               (I->Ops[0]->Width + 1)));
      break;
    case Inst::CtPop:
      Result = llvm::ConstantRange(llvm::APInt(I->Width, isReservedConst(I->Ops[0]) ? 1 : 0),
                                   llvm::APInt(I->Width, I->Ops[0]->Width + 1));
      break;
    case Inst::Phi:
      Result = CR0.unionWith(CR1);
      break;
    case Inst::Select:
      if (CR0.getSetSize() == 1) {
        if (CR0.contains(APInt(1, 1)))
          Result = CR1;
        else if (CR0.contains(APInt(1, 0)))
          Result = CR2;
      } else {
        Result = CR1.unionWith(CR2);
      }
      break;
      //     case Inst::SDiv: {
      //       auto R0 = FindConstantRange(I->Ops[0], C);
      //       auto R1 = FindConstantRange(I->Ops[1], C);
      //       return R0.sdiv(R1); // unimplemented
      //     }
      // TODO: Xor pattern for not, truncs and extends, etc
    default:
      break;
    }

    CRCache.emplace(I, Result);
    return CRCache.at(I);
  }
#undef CR0
#undef CR1
#undef CR2
#undef VAL

  llvm::ConstantRange ConstantRangeAnalysis::findConstantRangeUsingSolver(Inst *I, Solver *S, std::vector<InstMapping> &PCs) {
    // FIXME implement this
    llvm::ConstantRange Result(I->Width);
    return Result;
  }
}
