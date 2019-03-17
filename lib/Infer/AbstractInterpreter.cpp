#include "souper/Infer/Interpreter.h"
#include "souper/Infer/AbstractInterpreter.h"

#include <iostream>

namespace souper {

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

      auto trailingZeros0 = lhs.countMinTrailingZeros();
      auto trailingZeros1 = rhs.countMinTrailingZeros();
      //std::cout <<  lhs.getBitWidth() << " " << trailingZeros0 << " " << trailingZeros1 << std::endl;
      Result.Zero.setLowBits(std::min(trailingZeros0 + trailingZeros1, lhs.getBitWidth()));

      // check for leading zeros
      auto lz0 = lhs.getBitWidth() - lhs.countMinLeadingZeros();
      auto lz1 = rhs.getBitWidth() - rhs.countMinLeadingZeros();
      auto min = std::min(lz0, lz1);
      auto max = std::max(lz0, lz1);
      auto resultSize = max + (min - 1);
      if (resultSize < lhs.getBitWidth())
	Result.Zero.setHighBits(lhs.getBitWidth() - resultSize);

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
      auto Op0KB = lhs;
      auto Op1KB = rhs;

      Op0KB.One &= Op1KB.One;
      Op0KB.Zero |= Op1KB.Zero;
      return Op0KB;
    }
    llvm::KnownBits or_(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      auto Op0KB = lhs;
      auto Op1KB = rhs;

      Op0KB.One |= Op1KB.One;
      Op0KB.Zero &= Op1KB.Zero;
      return Op0KB;
    }
    llvm::KnownBits xor_(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      auto Op0KB = lhs;
      auto Op1KB = rhs;
      llvm::APInt KnownZeroOut =
        (Op0KB.Zero & Op1KB.Zero) | (Op0KB.One & Op1KB.One);
      Op0KB.One = (Op0KB.Zero & Op1KB.One) | (Op0KB.One & Op1KB.Zero);
      Op0KB.Zero = std::move(KnownZeroOut);
      // ^ logic copied from LLVM ValueTracking.cpp
      return Op0KB;
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
      } else if (!rhs.isZero()) {
	auto confirmedTrailingZeros = rhs.countMinTrailingZeros();
	auto minNum = 1 << (confirmedTrailingZeros - 1);
	// otherwise, it's poison
	if (minNum < width)
	  Result.Zero.setLowBits(minNum);
      }

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
      } else if (!rhs.isZero()) {
	auto confirmedTrailingZeros = rhs.countMinTrailingZeros();
	auto minNum = 1 << (confirmedTrailingZeros - 1);
	// otherwise, it's poison
	if (minNum < width)
	  Result.Zero.setHighBits(minNum);
      }

      return Result;
    }
    llvm::KnownBits ashr(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs) {
      llvm::KnownBits Result(lhs.getBitWidth());
      const auto width = Result.getBitWidth();

      auto confirmedTrailingZeros = rhs.countMinTrailingZeros();
      auto minNum = 1 << (confirmedTrailingZeros - 1);
      if (lhs.One.isSignBitSet()) {
	// confirmed: sign bit = 1
	if (minNum < width)
	  Result.One.setHighBits(minNum + 1);
      } else if (lhs.Zero.isSignBitSet()) {
	// confirmed: sign bit = 0
	if (minNum < width)
	  Result.Zero.setHighBits(minNum + 1);
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
      auto Op0KB = lhs;
      auto Op1KB = rhs;
      llvm::APInt Cond = (Op0KB.Zero & ~Op1KB.One) | (Op0KB.One & ~Op1KB.Zero);
      bool Conflict = Cond != 0;
      if (Conflict) {
        Result.One.setBit(0);
        return Result;
      }
      return Result;
    }
  }

  bool isReservedConst(Inst *I) {
    return I->K == Inst::ReservedConst ||
          (I->K == Inst::Var &&
          (I->Name.find(ReservedConstPrefix) != std::string::npos));
  }

  bool isReservedInst(Inst *I) {
    return I->K == Inst::ReservedInst ||
          (I->K == Inst::Var &&
          (I->Name.find(ReservedInstPrefix) != std::string::npos));
  }

  bool hasReservedHelper(Inst *I, std::set<Inst *> &Visited,
                         bool ConsiderConsts,
                         bool ConsiderHoles) {
    if (ConsiderConsts && isReservedConst(I)) {
      return true;
    }
    if (ConsiderHoles && isReservedInst(I)) {
      return true;
    }
    if (Visited.insert(I).second)
      for (auto Op : I->Ops)
        if (hasReservedHelper(Op, Visited, ConsiderConsts, ConsiderHoles))
          return true;
    return false;
  }

  bool isConcrete(Inst *I, bool ConsiderConsts, bool ConsiderHoles) {
    std::set<Inst *> Visited;
    return !hasReservedHelper(I, Visited, ConsiderConsts, ConsiderHoles);
  }

  EvalValue getValue(Inst *I, ValueCache &C, bool PartialEval) {
    if (I->K == Inst::Const)// || I->K == Inst::Var)
      return {I->Val};
    if (C.find(I) != C.end()) {
      return C[I];
    } else {
      if (PartialEval && isConcrete(I)) {
        return evaluateInst(I, C);
      }
    }
    // unimplemented
    return EvalValue();
  }

#define KB0 findKnownBits(I->Ops[0], C, PartialEval)
#define KB1 findKnownBits(I->Ops[1], C, PartialEval)
#define VAL(INST) getValue(INST, C, PartialEval)

  llvm::KnownBits findKnownBits(Inst *I, ValueCache &C, bool PartialEval) {
    llvm::KnownBits Result(I->Width);

/*    if (PartialEval && isConcrete(I)) {
      auto RootVal = evaluateInst(I, C);
      if (RootVal.hasValue()) {
        Result.One = RootVal.getValue();
        Result.Zero = ~RootVal.getValue();
        return Result;
      }
    }
*/

    EvalValue RootVal = VAL(I);
    if (RootVal.hasValue()) {
      Result.One = RootVal.getValue();
      Result.Zero = ~RootVal.getValue();
      return Result;
    } //else { //if (isReservedConst(I)) {
      // we don't synthesize number 0 as a constant
      // how to express that?
    //return Result;
    //}

    for (auto Op : I->Ops) {
      if (findKnownBits(Op, C, PartialEval).hasConflict()) {
	assert(false && "Conflict KB");
      }
    }

    switch(I->K) {
    case Inst::Const:
    case Inst::Var : {
      EvalValue V = VAL(I);
      if (V.hasValue()) {
        Result.One = V.getValue();
        Result.Zero = ~V.getValue();
        return Result;
      } else {
        return Result;
      }
    }
//   case Phi:
//     return "phi";
    case Inst::AddNUW :
    case Inst::AddNW :
    case Inst::Add: {
      return BinaryTransferFunctionsKB::add(KB0, KB1);
    }
    case Inst::AddNSW: {
      return BinaryTransferFunctionsKB::addnsw(KB0, KB1);
    }
    case Inst::SubNUW :
    case Inst::SubNW :
    case Inst::Sub: {
      return BinaryTransferFunctionsKB::sub(KB0, KB1);
    }
    case Inst::SubNSW: {
      return BinaryTransferFunctionsKB::subnsw(KB0, KB1);
    }
    case Inst::Mul: {
      return BinaryTransferFunctionsKB::mul(KB0, KB1);
    }
//   case MulNSW:
//     return "mulnsw";
//   case MulNUW:
//     return "mulnuw";
//   case MulNW:
//     return "mulnw";
    case Inst::UDiv: {
      return BinaryTransferFunctionsKB::udiv(KB0, KB1);

    }
//   case SDiv:
//     return "sdiv";
//   case UDivExact:
//     return "udivexact";
//   case SDivExact:
//     return "sdivexact";
    case Inst::URem: {
      return BinaryTransferFunctionsKB::urem(KB0, KB1);
    }
//   case SRem:
//     return "srem";
    case Inst::And : {
      return BinaryTransferFunctionsKB::and_(KB0, KB1);
    }
    case Inst::Or : {
      return BinaryTransferFunctionsKB::or_(KB0, KB1);
    }
    case Inst::Xor : {
      return BinaryTransferFunctionsKB::xor_(KB0, KB1);
    }
    case Inst::ShlNSW :
    case Inst::ShlNUW :
    case Inst::ShlNW : // TODO: Rethink if these make sense
    case Inst::Shl : {
      /*
      auto Op0KB = KB0;
      auto Op1V = VAL(I->Ops[1]);

      if (Op1V.hasValue()) {
        auto Val = Op1V.getValue().getLimitedValue();
        if (Val < 0 || Val >= I->Width) {
          return Result;
        }
        Op0KB.One <<= Val;
        Op0KB.Zero <<= Val;
        Op0KB.Zero.setLowBits(Val);
        // setLowBits takes an unsigned int, so getLimitedValue is harmless
        return Op0KB;
      } else if (isReservedConst(I->Ops[1])) {
        Result.Zero.setLowBits(1);
      } else if (!KB1.isZero()) {
       auto confirmedTrailingZeros = KB1.countMinTrailingZeros();
       auto minNum = 1 << (confirmedTrailingZeros - 1);
       // otherwise, it's poison
       if (minNum < I->Width)
         Result.Zero.setLowBits(minNum);
      }

      return Result;
      */
      return BinaryTransferFunctionsKB::shl(KB0, KB1);
    }
    case Inst::LShr : {
      /*
      auto Op0KB = KB0;
      auto Op1V = VAL(I->Ops[1]);
      if (Op1V.hasValue()) {
        auto Val = Op1V.getValue().getLimitedValue();
        if (Val < 0 || Val >= I->Width) {
          return Result;
        }
        Op0KB.One.lshrInPlace(Val);
        Op0KB.Zero.lshrInPlace(Val);
        Op0KB.Zero.setHighBits(Val);
        // setHighBits takes an unsigned int, so getLimitedValue is harmless
        return Op0KB;
      } else if (isReservedConst(I->Ops[1])) {
       // we don't synthesize '0' as constant
       Result.Zero.setHighBits(1);
      } else if (!KB1.isZero()) {
       auto confirmedTrailingZeros = KB1.countMinTrailingZeros();
       auto minNum = 1 << (confirmedTrailingZeros - 1);
       // otherwise, it's poison
       if (minNum < I->Width)
         Result.Zero.setHighBits(minNum);
      }

      return Result;
      */
      return BinaryTransferFunctionsKB::lshr(KB0, KB1);
    }
//   case LShrExact:
//     return "lshrexact";
    case Inst::AShr: {
      return BinaryTransferFunctionsKB::ashr(KB0, KB1);
    }
//   case AShrExact:
//     return "ashrexact";
//   case Select:
//     return "select";
    case Inst::ZExt: {
      return KB0.zext(I->Width);
    }
    case Inst::SExt: {
      return KB0.sext(I->Width);
    }
    case Inst::Trunc: {
      return KB0.trunc(I->Width);
    }

    case Inst::Eq: {
      Inst *Constant = nullptr;
      llvm::KnownBits Other;
      if (isReservedConst(I->Ops[0])) {
        Constant = I->Ops[0];
        Other = KB1;
      } else if (isReservedConst(I->Ops[1])) {
        Constant = I->Ops[1];
        Other = KB0;
      } else {
        return Result;
      }

      // Constants are never equal to 0
      if (Other.Zero.isAllOnesValue()) {
        Result.Zero.setBit(0);
        return Result;
      } else {
        return Result;
      }

      return BinaryTransferFunctionsKB::eq(KB0, KB1);
    }
    case Inst::Ne: {
      return BinaryTransferFunctionsKB::ne(KB0, KB1);
    }
//   case Ult:
//     return "ult";
//   case Slt:
//     return "slt";
//   case Ule:
//     return "ule";
//   case Sle:
//     return "sle";
//   case CtPop:
//     return "ctpop";
    case Inst::BSwap: {
      auto Op0KB = KB0;
      Op0KB.One = Op0KB.One.byteSwap();
      Op0KB.Zero = Op0KB.Zero.byteSwap();
      return Op0KB;
    }
    case Inst::BitReverse: {
      auto Op0KB = KB0;
      Op0KB.One = Op0KB.One.reverseBits();
      Op0KB.Zero = Op0KB.Zero.reverseBits();
      return Op0KB;
    }
//   case Cttz:
//     return "cttz";
//   case Ctlz:
//     return "ctlz";
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
      return Result;
    }
  }

#undef KB0
#undef KB1

#define CR0 findConstantRange(I->Ops[0], C, PartialEval)
#define CR1 findConstantRange(I->Ops[1], C, PartialEval)
#define CR2 findConstantRange(I->Ops[2], C, PartialEval)

  llvm::ConstantRange findConstantRange(Inst *I,
                                        ValueCache &C, bool PartialEval) {
    llvm::ConstantRange Result(I->Width);

    if (PartialEval && isConcrete(I)) {
      auto RootVal = evaluateInst(I, C);
      if (RootVal.hasValue()) {
        return llvm::ConstantRange(RootVal.getValue());
      }
    }

    switch (I->K) {
    case Inst::Const:
    case Inst::Var : {
      EvalValue V = VAL(I);
      if (V.hasValue()) {
        return llvm::ConstantRange(V.getValue());
      } else {
        if (isReservedConst(I)) {
          return llvm::ConstantRange(llvm::APInt(I->Width, 0)).inverse();
        }
        return Result; // Whole range
      }
    }
    case Inst::Trunc: {
      return CR0.truncate(I->Width);
    }
    case Inst::SExt: {
      return CR0.signExtend(I->Width);
    }
    case Inst::ZExt: {
      return CR0.zeroExtend(I->Width);
    }
    case souper::Inst::AddNUW :
    case souper::Inst::AddNW : // TODO: Rethink if these make sense
    case Inst::Add: {
      return CR0.add(CR1);
    }
    case Inst::AddNSW: {
      auto V1 = VAL(I->Ops[1]);
      if (V1.hasValue()) {
        return CR0.addWithNoSignedWrap(V1.getValue());
      } else {
        return Result; // full range, can we do better?
      }
    }
    case souper::Inst::SubNSW :
    case souper::Inst::SubNUW :
    case souper::Inst::SubNW : // TODO: Rethink if these make sense
    case Inst::Sub: {
      return CR0.sub(CR1);
    }
    case souper::Inst::MulNSW :
    case souper::Inst::MulNUW :
    case souper::Inst::MulNW : // TODO: Rethink if these make sense
    case Inst::Mul: {
      return CR0.multiply(CR1);
    }
    case Inst::And: {
      return CR0.binaryAnd(CR1);
    }
    case Inst::Or: {
      return CR0.binaryOr(CR1);
    }
    case souper::Inst::ShlNSW :
    case souper::Inst::ShlNUW :
    case souper::Inst::ShlNW : // TODO: Rethink if these make sense
    case Inst::Shl: {
      return CR0.shl(CR1);
    }
    case Inst::AShr: {
      return CR0.ashr(CR1);
    }
    case Inst::LShr: {
      return CR0.lshr(CR1);
    }
    case Inst::UDiv: {
      return CR0.udiv(CR1);
    }
    case Inst::Ctlz:
    case Inst::Cttz:
    case Inst::CtPop: {
      return llvm::ConstantRange(llvm::APInt(I->Width, 0),
                                 llvm::APInt(I->Width, I->Ops[0]->Width + 1));
    }
    case Inst::Select: {
      return CR1.unionWith(CR2);
    }
      //     case Inst::SDiv: {
      //       auto R0 = FindConstantRange(I->Ops[0], C);
      //       auto R1 = FindConstantRange(I->Ops[1], C);
      //       return R0.sdiv(R1); // unimplemented
      //     }
      // TODO: Xor pattern for not, truncs and extends, etc
    default:
      return Result;
    }
  }
#undef CR0
#undef CR1
#undef CR2
#undef VAL
}
