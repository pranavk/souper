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

#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"

#include "souper/Infer/Interpreter.h"
#include "souper/Infer/AbstractInterpreter.h"
#include "souper/Inst/Inst.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace souper;

// width used for xfer tests
constexpr int WIDTH = 4;

namespace {

  bool nextKB(llvm::KnownBits &x) {
    for (int i = 0; i < x.getBitWidth(); i++) {
      if (!x.Zero[i] && !x.One[i]) {
	x.Zero.setBit(i);
	return true;
      }
      if (x.Zero[i] && !x.One[i]) {
	x.Zero.clearBit(i);
	x.One.setBit(i);
	return true;
      }
      if (!x.Zero[i] && x.One[i]) {
	x.Zero.clearBit(i);
	x.One.clearBit(i);
	continue;
      }
      assert(false && "error in nextKB");
    }
    return false;
  }

  enum class Tristate { Unknown, False, True };

  bool isConcrete(KnownBits x) { return (x.Zero | x.One).isAllOnesValue(); }

  KnownBits merge(KnownBits a, KnownBits b) {
    assert(a.getBitWidth() == b.getBitWidth() && "Can only merge KnownBits of same width.");

    KnownBits res(a.getBitWidth());
    for (unsigned i = 0; i < a.getBitWidth(); i++) {
      if (a.One[i] == b.One[i] && b.One[i])
	a.One.setBit(i);
      if (a.Zero[i] == b.Zero[i] && a.Zero[i])
	a.Zero.setBit(i);
    }
    return res;
  }

  KnownBits setLowest(KnownBits x) {
    for (int i = 0; i < x.getBitWidth(); i++) {
      if (!x.Zero[i] && !x.One[i]) {
	x.One.setBit(i);
	return x;
      }
    }
    assert(false);
    return x;
  }

  KnownBits clearLowest(KnownBits x) {
    for (int i = 0; i < x.getBitWidth(); i++) {
      if (!x.Zero[i] && !x.One[i]) {
	x.Zero.setBit(i);
	return x;
      }
    }
    assert(false);
    return x;
  }

  KnownBits bruteForce(KnownBits x, KnownBits y, Inst::Kind Pred) {
    if (!isConcrete(x))
      return merge(bruteForce(setLowest(x), y, Pred),
		   bruteForce(clearLowest(x), y, Pred));
    if (!isConcrete(y))
      return merge(bruteForce(x, setLowest(y), Pred),
		   bruteForce(x, clearLowest(y), Pred));
    auto xc = x.getConstant();
    auto yc = y.getConstant();

    KnownBits res(x.getBitWidth());
    switch (Pred) {
    case Inst::Add:
    {
      auto rc = xc + yc;
      res.One = rc;
      res.Zero = ~rc;
    }
    break;
    case Inst::Sub:
    {
      auto rc = xc - yc;
      res.One = rc;
      res.Zero = ~rc;
    }
    break;
    default:
      break;
      //llvm::report_fatal_error("no implementation for predicate");
    }
    return res;
  }

  void testFn(Inst::Kind pred) {
    llvm::KnownBits x(WIDTH);
    do {
      llvm::KnownBits y(WIDTH);
      do {
	KnownBits Res1;
	switch(pred) {
	case Inst::Add:
	  Res1 = BinaryTransferFunctionsKB::add(x, y);
	  break;
	}

	KnownBits Res2 = bruteForce(x, y, pred);
	if (Res1.One != Res2.One || Res1.Zero != Res2.Zero)
	  assert(false && "unsound!!!");
      } while(nextKB(y));
    } while(nextKB(x));
  }

} // anon

TEST(InterpreterTests, KBTransferFunctions) {
  testFn(Inst::Add);
}

TEST(InterpreterTests, KnownBits) {
  InstContext IC;

  Inst *I1 = IC.getConst(llvm::APInt(64, 5));

  souper::ValueCache C;
  auto KB = souper::findKnownBits(I1, C);
  ASSERT_EQ(KB.One, 5);
  ASSERT_EQ(KB.Zero, ~5);

  Inst *I2 = IC.getInst(Inst::Var, 64, {});
  Inst *I3 = IC.getConst(llvm::APInt(64, 0xFF));
  Inst *I4 = IC.getInst(Inst::And, 64, {I2, I3});
  KB = souper::findKnownBits(I4, C, /*PartialEval=*/false);
  ASSERT_EQ(KB.One, 0);
  ASSERT_EQ(KB.Zero, ~0xFF);

  Inst *I5 = IC.getInst(Inst::Or, 64, {I2, I1});
  KB = souper::findKnownBits(I5, C, /*PartialEval=*/false);
  ASSERT_EQ(KB.One, 5);
  ASSERT_EQ(KB.Zero, 0);

  Inst *I6 = IC.getInst(Inst::Shl, 64, {I2, I1});
  KB = souper::findKnownBits(I6, C, /*PartialEval=*/false);
  ASSERT_EQ(KB.One, 0);
  ASSERT_EQ(KB.Zero, 31);
}

TEST(InterpreterTests, ConstantRange) {
  InstContext IC;

  Inst *I1 = IC.getConst(llvm::APInt(64, 5));

  souper::ValueCache C;
  auto CR = souper::findConstantRange(I1, C, /*PartialEval=*/false);
  ASSERT_EQ(CR.getLower(), 5);
  ASSERT_EQ(CR.getUpper(), 6);

  Inst *I2 = IC.getInst(Inst::Var, 64, {});
  Inst *I3 = IC.getConst(llvm::APInt(64, 0xFF));
  Inst *I4 = IC.getInst(Inst::And, 64, {I2, I3});
  CR = souper::findConstantRange(I4, C, /*PartialEval=*/false);
  ASSERT_EQ(CR.getLower(), 0);
  ASSERT_EQ(CR.getUpper(), 0xFF + 1);

  Inst *I5 = IC.getInst(Inst::Add, 64, {I4, I1});
  CR = souper::findConstantRange(I5, C, /*PartialEval=*/false);
  ASSERT_EQ(CR.getLower(), 5);
  ASSERT_EQ(CR.getUpper(), 0xFF + 5 + 1);
}
