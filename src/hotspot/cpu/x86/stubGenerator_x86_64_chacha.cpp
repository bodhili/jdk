/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "runtime/stubRoutines.hpp"
#include "macroAssembler_x86.hpp"
#include "stubGenerator_x86_64.hpp"

#define __ _masm->

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif // PRODUCT

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

// Constants

/**
 * This AVX/AVX2 add mask generation can be used for multiple duties:
 *      1.) Provide +0/+1 counter increments by loading 256 bits
 *          at offset 0
 *      2.) Provide +2/+2 counter increments for the second set
 *          of 4 AVX2 registers at offset 32 (256-bit load)
 *      3.) Provide a +1 increment for the second set of 4 AVX
 *          registers at offset 16 (128-bit load)
 */
ATTRIBUTE_ALIGNED(64) uint64_t CC20_COUNTER_ADD_AVX[] = {
    0x0000000000000000UL, 0x0000000000000000UL,
    0x0000000000000001UL, 0x0000000000000000UL,
    0x0000000000000002UL, 0x0000000000000000UL,
    0x0000000000000002UL, 0x0000000000000000UL,
};
static address chacha20_ctradd_avx() {
  return (address)CC20_COUNTER_ADD_AVX;
}

/**
 * Add masks for 4-block ChaCha20 Block calculations
 * The first 512 bits creates a +0/+1/+2/+3 add overlay.
 * The second 512 bits is a +4/+4/+4/+4 add overlay.  This
 * can be used to increment the counter fields for the next 4 blocks.
 */
ATTRIBUTE_ALIGNED(64) uint64_t CC20_COUNTER_ADD_AVX512[] = {
    0x0000000000000000UL, 0x0000000000000000UL,
    0x0000000000000001UL, 0x0000000000000000UL,
    0x0000000000000002UL, 0x0000000000000000UL,
    0x0000000000000003UL, 0x0000000000000000UL,

    0x0000000000000004UL, 0x0000000000000000UL,
    0x0000000000000004UL, 0x0000000000000000UL,
    0x0000000000000004UL, 0x0000000000000000UL,
    0x0000000000000004UL, 0x0000000000000000UL
};
static address chacha20_ctradd_avx512() {
  return (address)CC20_COUNTER_ADD_AVX512;
}

void StubGenerator::generate_chacha_stubs() {
  // Generate ChaCha20 intrinsics code
  if (UseChaCha20Intrinsics) {
    if (VM_Version::supports_evex()) {
      StubRoutines::_chacha20Block = generate_chacha20Block_avx512();
    } else {    // Either AVX or AVX2 is supported
      StubRoutines::_chacha20Block = generate_chacha20Block_avx();
    }
  }
}

/* The 2-block AVX/AVX2-enabled ChaCha20 block function implementation */
address StubGenerator::generate_chacha20Block_avx() {
  __ align(CodeEntryAlignment);
  StubCodeMark mark(this, "StubRoutines", "chacha20Block");
  address start = __ pc();

  Label L_twoRounds;
  const Register state        = c_rarg0;
  const Register result       = c_rarg1;
  const Register loopCounter  = r8;

  const XMMRegister aState = xmm0;
  const XMMRegister bState = xmm1;
  const XMMRegister cState = xmm2;
  const XMMRegister dState = xmm3;
  const XMMRegister a1Vec = xmm4;
  const XMMRegister b1Vec = xmm5;
  const XMMRegister c1Vec = xmm6;
  const XMMRegister d1Vec = xmm7;
  const XMMRegister a2Vec = xmm8;
  const XMMRegister b2Vec = xmm9;
  const XMMRegister c2Vec = xmm10;
  const XMMRegister d2Vec = xmm11;
  const XMMRegister scratch = xmm12;
  const XMMRegister d2State = xmm13;

  int vector_len;
  int outlen;

  // This function will only be called if AVX2 or AVX are supported
  // AVX512 uses a different function.
  if (VM_Version::supports_avx2()) {
    vector_len = Assembler::AVX_256bit;
    outlen = 256;
  } else if (VM_Version::supports_avx()) {
    vector_len = Assembler::AVX_128bit;
    outlen = 128;
  }

  __ enter();

  // Load the initial state in columnar orientation and then copy
  // that starting state to the working register set.
  // Also load the address of the add mask for later use in handling
  // multi-block counter increments.
  __ lea(rax, ExternalAddress(chacha20_ctradd_avx()));
  if (vector_len == Assembler::AVX_128bit) {
    __ movdqu(aState, Address(state, 0));       // Bytes 0 - 15 -> a1Vec
    __ movdqu(bState, Address(state, 16));      // Bytes 16 - 31 -> b1Vec
    __ movdqu(cState, Address(state, 32));      // Bytes 32 - 47 -> c1Vec
    __ movdqu(dState, Address(state, 48));      // Bytes 48 - 63 -> d1Vec

    __ movdqu(a1Vec, aState);
    __ movdqu(b1Vec, bState);
    __ movdqu(c1Vec, cState);
    __ movdqu(d1Vec, dState);

    __ movdqu(a2Vec, aState);
    __ movdqu(b2Vec, bState);
    __ movdqu(c2Vec, cState);
    __ vpaddd(d2State, dState, Address(rax, 16), vector_len);
    __ movdqu(d2Vec, d2State);
  } else {
    // We will broadcast each 128-bit segment of the state array into
    // the high and low halves of ymm state registers.  Then apply the add
    // mask to the dState register.  These will then be copied into the
    // a/b/c/d1Vec working registers.
    __ vbroadcastf128(aState, Address(state, 0), vector_len);
    __ vbroadcastf128(bState, Address(state, 16), vector_len);
    __ vbroadcastf128(cState, Address(state, 32), vector_len);
    __ vbroadcastf128(dState, Address(state, 48), vector_len);
    __ vpaddd(dState, dState, Address(rax, 0), vector_len);
    __ vpaddd(d2State, dState, Address(rax, 32), vector_len);

    __ vmovdqu(a1Vec, aState);
    __ vmovdqu(b1Vec, bState);
    __ vmovdqu(c1Vec, cState);
    __ vmovdqu(d1Vec, dState);

    __ vmovdqu(a2Vec, aState);
    __ vmovdqu(b2Vec, bState);
    __ vmovdqu(c2Vec, cState);
    __ vmovdqu(d2Vec, d2State);
  }

  __ movl(loopCounter, 10);                   // Set 10 2-round iterations
  __ BIND(L_twoRounds);

  // The first quarter round macro call covers the first 4 QR operations:
  //  Qround(state, 0, 4, 8,12)
  //  Qround(state, 1, 5, 9,13)
  //  Qround(state, 2, 6,10,14)
  //  Qround(state, 3, 7,11,15)
  __ cc20_quarter_round_avx(a1Vec, b1Vec, c1Vec, d1Vec, scratch, vector_len);
  __ cc20_quarter_round_avx(a2Vec, b2Vec, c2Vec, d2Vec, scratch, vector_len);

  // Shuffle the b1Vec/c1Vec/d1Vec to reorganize the state vectors
  // to diagonals.  The a1Vec does not need to change orientation.
  __ cc20_shift_lane_org(b1Vec, c1Vec, d1Vec, vector_len, true);
  __ cc20_shift_lane_org(b2Vec, c2Vec, d2Vec, vector_len, true);

  // The second set of operations on the vectors covers the second 4 quarter
  // round operations, now acting on the diagonals:
  //  Qround(state, 0, 5,10,15)
  //  Qround(state, 1, 6,11,12)
  //  Qround(state, 2, 7, 8,13)
  //  Qround(state, 3, 4, 9,14)
  __ cc20_quarter_round_avx(a1Vec, b1Vec, c1Vec, d1Vec, scratch, vector_len);
  __ cc20_quarter_round_avx(a2Vec, b2Vec, c2Vec, d2Vec, scratch, vector_len);

  // Before we start the next iteration, we need to perform shuffles
  // on the b/c/d vectors to move them back to columnar organizations
  // from their current diagonal orientation.
  __ cc20_shift_lane_org(b1Vec, c1Vec, d1Vec, vector_len, false);
  __ cc20_shift_lane_org(b2Vec, c2Vec, d2Vec, vector_len, false);

  __ decrement(loopCounter);
  __ jcc(Assembler::notZero, L_twoRounds);

  // Add the original start state back into the current state.
  __ vpaddd(a1Vec, a1Vec, aState, vector_len);
  __ vpaddd(b1Vec, b1Vec, bState, vector_len);
  __ vpaddd(c1Vec, c1Vec, cState, vector_len);
  __ vpaddd(d1Vec, d1Vec, dState, vector_len);

  __ vpaddd(a2Vec, a2Vec, aState, vector_len);
  __ vpaddd(b2Vec, b2Vec, bState, vector_len);
  __ vpaddd(c2Vec, c2Vec, cState, vector_len);
  __ vpaddd(d2Vec, d2Vec, d2State, vector_len);

  // Write the data to the keystream array
  if (vector_len == Assembler::AVX_128bit) {
    __ movdqu(Address(result, 0), a1Vec);
    __ movdqu(Address(result, 16), b1Vec);
    __ movdqu(Address(result, 32), c1Vec);
    __ movdqu(Address(result, 48), d1Vec);
    __ movdqu(Address(result, 64), a2Vec);
    __ movdqu(Address(result, 80), b2Vec);
    __ movdqu(Address(result, 96), c2Vec);
    __ movdqu(Address(result, 112), d2Vec);
  } else {
    // Each half of the YMM has to be written 64 bytes apart from
    // each other in memory so the final keystream buffer holds
    // two consecutive keystream blocks.
    __ vextracti128(Address(result, 0), a1Vec, 0);
    __ vextracti128(Address(result, 64), a1Vec, 1);
    __ vextracti128(Address(result, 16), b1Vec, 0);
    __ vextracti128(Address(result, 80), b1Vec, 1);
    __ vextracti128(Address(result, 32), c1Vec, 0);
    __ vextracti128(Address(result, 96), c1Vec, 1);
    __ vextracti128(Address(result, 48), d1Vec, 0);
    __ vextracti128(Address(result, 112), d1Vec, 1);

    __ vextracti128(Address(result, 128), a2Vec, 0);
    __ vextracti128(Address(result, 192), a2Vec, 1);
    __ vextracti128(Address(result, 144), b2Vec, 0);
    __ vextracti128(Address(result, 208), b2Vec, 1);
    __ vextracti128(Address(result, 160), c2Vec, 0);
    __ vextracti128(Address(result, 224), c2Vec, 1);
    __ vextracti128(Address(result, 176), d2Vec, 0);
    __ vextracti128(Address(result, 240), d2Vec, 1);
  }

  // This function will always write 128 or 256 bytes into the
  // key stream buffer, depending on the length of the SIMD
  // registers.  That length should be returned through %rax.
  __ mov64(rax, outlen);

  __ leave();
  __ ret(0);
  return start;
}

/* The 4-block AVX512-enabled ChaCha20 block function implementation */
address StubGenerator::generate_chacha20Block_avx512() {
  __ align(CodeEntryAlignment);
  StubCodeMark mark(this, "StubRoutines", "chacha20Block");
  address start = __ pc();

  Label L_twoRounds;
  const Register state        = c_rarg0;
  const Register result       = c_rarg1;
  const Register loopCounter  = r8;

  const XMMRegister aState = xmm0;
  const XMMRegister bState = xmm1;
  const XMMRegister cState = xmm2;
  const XMMRegister dState = xmm3;
  const XMMRegister a1Vec = xmm4;
  const XMMRegister b1Vec = xmm5;
  const XMMRegister c1Vec = xmm6;
  const XMMRegister d1Vec = xmm7;
  const XMMRegister a2Vec = xmm8;
  const XMMRegister b2Vec = xmm9;
  const XMMRegister c2Vec = xmm10;
  const XMMRegister d2Vec = xmm11;
  const XMMRegister a3Vec = xmm12;
  const XMMRegister b3Vec = xmm13;
  const XMMRegister c3Vec = xmm14;
  const XMMRegister d3Vec = xmm15;
  const XMMRegister a4Vec = xmm16;
  const XMMRegister b4Vec = xmm17;
  const XMMRegister c4Vec = xmm18;
  const XMMRegister d4Vec = xmm19;
  const XMMRegister d2State = xmm20;
  const XMMRegister d3State = xmm21;
  const XMMRegister d4State = xmm22;
  const XMMRegister scratch = xmm23;

  __ enter();

  // Load the initial state in columnar orientation.
  // We will broadcast each 128-bit segment of the state array into
  // all four double-quadword slots on ZMM State registers.  They will
  // be copied into the working ZMM registers and then added back in
  // at the very end of the block function.  The add mask should be
  // applied to the dState register so it does not need to be fetched
  // when adding the start state back into the final working state.
  __ lea(rax, ExternalAddress(chacha20_ctradd_avx512()));
  __ evbroadcasti32x4(aState, Address(state, 0), Assembler::AVX_512bit);
  __ evbroadcasti32x4(bState, Address(state, 16), Assembler::AVX_512bit);
  __ evbroadcasti32x4(cState, Address(state, 32), Assembler::AVX_512bit);
  __ evbroadcasti32x4(dState, Address(state, 48), Assembler::AVX_512bit);
  __ vpaddd(dState, dState, Address(rax, 0), Assembler::AVX_512bit);
  __ evmovdqul(scratch, Address(rax, 64), Assembler::AVX_512bit);
  __ vpaddd(d2State, dState, scratch, Assembler::AVX_512bit);
  __ vpaddd(d3State, d2State, scratch, Assembler::AVX_512bit);
  __ vpaddd(d4State, d3State, scratch, Assembler::AVX_512bit);

  __ evmovdqul(a1Vec, aState, Assembler::AVX_512bit);
  __ evmovdqul(b1Vec, bState, Assembler::AVX_512bit);
  __ evmovdqul(c1Vec, cState, Assembler::AVX_512bit);
  __ evmovdqul(d1Vec, dState, Assembler::AVX_512bit);

  __ evmovdqul(a2Vec, aState, Assembler::AVX_512bit);
  __ evmovdqul(b2Vec, bState, Assembler::AVX_512bit);
  __ evmovdqul(c2Vec, cState, Assembler::AVX_512bit);
  __ evmovdqul(d2Vec, d2State, Assembler::AVX_512bit);

  __ evmovdqul(a3Vec, aState, Assembler::AVX_512bit);
  __ evmovdqul(b3Vec, bState, Assembler::AVX_512bit);
  __ evmovdqul(c3Vec, cState, Assembler::AVX_512bit);
  __ evmovdqul(d3Vec, d3State, Assembler::AVX_512bit);

  __ evmovdqul(a4Vec, aState, Assembler::AVX_512bit);
  __ evmovdqul(b4Vec, bState, Assembler::AVX_512bit);
  __ evmovdqul(c4Vec, cState, Assembler::AVX_512bit);
  __ evmovdqul(d4Vec, d4State, Assembler::AVX_512bit);

  __ movl(loopCounter, 10);                       // Set 10 2-round iterations
  __ BIND(L_twoRounds);

  // The first set of operations on the vectors covers the first 4 quarter
  // round operations:
  //  Qround(state, 0, 4, 8,12)
  //  Qround(state, 1, 5, 9,13)
  //  Qround(state, 2, 6,10,14)
  //  Qround(state, 3, 7,11,15)
  __ cc20_quarter_round_avx(a1Vec, b1Vec, c1Vec, d1Vec, scratch, Assembler::AVX_512bit);
  __ cc20_quarter_round_avx(a2Vec, b2Vec, c2Vec, d2Vec, scratch, Assembler::AVX_512bit);
  __ cc20_quarter_round_avx(a3Vec, b3Vec, c3Vec, d3Vec, scratch, Assembler::AVX_512bit);
  __ cc20_quarter_round_avx(a4Vec, b4Vec, c4Vec, d4Vec, scratch, Assembler::AVX_512bit);

  // Shuffle the b1Vec/c1Vec/d1Vec to reorganize the state vectors
  // to diagonals.  The a1Vec does not need to change orientation.
  __ cc20_shift_lane_org(b1Vec, c1Vec, d1Vec, Assembler::AVX_512bit, true);
  __ cc20_shift_lane_org(b2Vec, c2Vec, d2Vec, Assembler::AVX_512bit, true);
  __ cc20_shift_lane_org(b3Vec, c3Vec, d3Vec, Assembler::AVX_512bit, true);
  __ cc20_shift_lane_org(b4Vec, c4Vec, d4Vec, Assembler::AVX_512bit, true);

  // The second set of operations on the vectors covers the second 4 quarter
  // round operations, now acting on the diagonals:
  //  Qround(state, 0, 5,10,15)
  //  Qround(state, 1, 6,11,12)
  //  Qround(state, 2, 7, 8,13)
  //  Qround(state, 3, 4, 9,14)
  __ cc20_quarter_round_avx(a1Vec, b1Vec, c1Vec, d1Vec, scratch, Assembler::AVX_512bit);
  __ cc20_quarter_round_avx(a2Vec, b2Vec, c2Vec, d2Vec, scratch, Assembler::AVX_512bit);
  __ cc20_quarter_round_avx(a3Vec, b3Vec, c3Vec, d3Vec, scratch, Assembler::AVX_512bit);
  __ cc20_quarter_round_avx(a4Vec, b4Vec, c4Vec, d4Vec, scratch, Assembler::AVX_512bit);

  // Before we start the next iteration, we need to perform shuffles
  // on the b/c/d vectors to move them back to columnar organizations
  // from their current diagonal orientation.
  __ cc20_shift_lane_org(b1Vec, c1Vec, d1Vec, Assembler::AVX_512bit, false);
  __ cc20_shift_lane_org(b2Vec, c2Vec, d2Vec, Assembler::AVX_512bit, false);
  __ cc20_shift_lane_org(b3Vec, c3Vec, d3Vec, Assembler::AVX_512bit, false);
  __ cc20_shift_lane_org(b4Vec, c4Vec, d4Vec, Assembler::AVX_512bit, false);

  __ decrement(loopCounter);
  __ jcc(Assembler::notZero, L_twoRounds);

  // Add the initial state now held on the a/b/c/dState registers to the
  // final working register values.  We will also add in the counter add
  // mask onto zmm3 after adding in the start state.
  __ vpaddd(a1Vec, a1Vec, aState, Assembler::AVX_512bit);
  __ vpaddd(b1Vec, b1Vec, bState, Assembler::AVX_512bit);
  __ vpaddd(c1Vec, c1Vec, cState, Assembler::AVX_512bit);
  __ vpaddd(d1Vec, d1Vec, dState, Assembler::AVX_512bit);

  __ vpaddd(a2Vec, a2Vec, aState, Assembler::AVX_512bit);
  __ vpaddd(b2Vec, b2Vec, bState, Assembler::AVX_512bit);
  __ vpaddd(c2Vec, c2Vec, cState, Assembler::AVX_512bit);
  __ vpaddd(d2Vec, d2Vec, d2State, Assembler::AVX_512bit);

  __ vpaddd(a3Vec, a3Vec, aState, Assembler::AVX_512bit);
  __ vpaddd(b3Vec, b3Vec, bState, Assembler::AVX_512bit);
  __ vpaddd(c3Vec, c3Vec, cState, Assembler::AVX_512bit);
  __ vpaddd(d3Vec, d3Vec, d3State, Assembler::AVX_512bit);

  __ vpaddd(a4Vec, a4Vec, aState, Assembler::AVX_512bit);
  __ vpaddd(b4Vec, b4Vec, bState, Assembler::AVX_512bit);
  __ vpaddd(c4Vec, c4Vec, cState, Assembler::AVX_512bit);
  __ vpaddd(d4Vec, d4Vec, d4State, Assembler::AVX_512bit);

  // Write the ZMM state registers out to the key stream buffer
  // Each ZMM is divided into 4 128-bit segments.  Each segment
  // is written to memory at 64-byte displacements from one
  // another.  The result is that all 4 blocks will be in their
  // proper order when serialized.
  __ cc20_keystream_collate_avx512(a1Vec, b1Vec, c1Vec, d1Vec, result, 0);
  __ cc20_keystream_collate_avx512(a2Vec, b2Vec, c2Vec, d2Vec, result, 256);
  __ cc20_keystream_collate_avx512(a3Vec, b3Vec, c3Vec, d3Vec, result, 512);
  __ cc20_keystream_collate_avx512(a4Vec, b4Vec, c4Vec, d4Vec, result, 768);

  // This function will always write 1024 bytes into the key stream buffer
  // and that length should be returned through %rax.
  __ mov64(rax, 1024);

  __ leave();
  __ ret(0);
  return start;
}

#undef __
