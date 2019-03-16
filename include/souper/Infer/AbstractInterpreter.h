#pragma once

#include "llvm/Support/KnownBits.h"

namespace souper {
  namespace BinaryTransferFunctionsKB {
    llvm::KnownBits add(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits addnsw(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits sub(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits subnsw(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits mul(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits udiv(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits urem(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits and_(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits or_(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits xor_(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits shl(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits lhsr(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits ashr(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits zext(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits sext(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits trunc(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits eq(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits ne(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits bswap(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
    llvm::KnownBits bitreverse(const llvm::KnownBits &lhs, const llvm::KnownBits &rhs);
  }
}
