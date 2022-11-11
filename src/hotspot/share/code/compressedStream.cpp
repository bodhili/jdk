/*
 * Copyright (c) 1997, 2022, Oracle and/or its affiliates. All rights reserved.
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
#include "code/compressedStream.hpp"
#include "utilities/ostream.hpp"
#include "utilities/moveBits.hpp"

jint CompressedReadStream::read_signed_int() {
  return UNSIGNED5::decode_sign(read_int());
}

// Compressing floats is simple, because the only common pattern
// is trailing zeroes.  (Compare leading sign bits on ints.)
// Since floats are left-justified, as opposed to right-justified
// ints, we can bit-reverse them in order to take advantage of int
// compression.  Since bit reversal converts trailing zeroes to
// leading zeroes, effect is better compression of those common
// 32-bit float values, such as integers or integers divided by
// powers of two, that have many trailing zeroes.
jfloat CompressedReadStream::read_float() {
  int rf = read_int();
  int f  = reverse_bits(rf);
  return jfloat_cast(f);
}

// The treatment of doubles is similar.  We could bit-reverse each
// entire 64-bit word, but it is almost as effective to bit-reverse
// the individual halves.  Since we are going to encode them
// separately as 32-bit halves anyway, it seems slightly simpler
// to reverse after splitting, and when reading reverse each
// half before joining them together.
jdouble CompressedReadStream::read_double() {
  jint rh = read_int();
  jint rl = read_int();
  jint h  = reverse_bits(rh);
  jint l  = reverse_bits(rl);
  return jdouble_cast(jlong_from(h, l));
}

// A 64-bit long is encoded into distinct 32-bit halves.  This saves
// us from having to define a 64-bit encoding and is almost as
// effective.  A modified LEB128 could encode longs into 9 bytes, and
// this technique maxes out at 10 bytes, so, if we didn't mind the
// extra complexity of another coding system, we could process 64-bit
// values as single units.  But, the complexity does not seem
// worthwhile.
jlong CompressedReadStream::read_long() {
  jint low  = read_signed_int();
  jint high = read_signed_int();
  return jlong_from(high, low);
}

CompressedWriteStream::CompressedWriteStream(int initial_size) : CompressedStream(NULL, 0) {
  _buffer   = NEW_RESOURCE_ARRAY(u_char, initial_size);
  _size     = initial_size;
  _position = 0;
}

void CompressedWriteStream::grow() {
  int nsize = _size * 2;
  const int min_expansion = UNSIGNED5::MAX_LENGTH;
  if (nsize < min_expansion*2) {
    nsize = min_expansion*2;
  }
  u_char* _new_buffer = NEW_RESOURCE_ARRAY(u_char, nsize);
  memcpy(_new_buffer, _buffer, _position);
  _buffer = _new_buffer;
  _size   = nsize;
}

void CompressedWriteStream::write_float(jfloat value) {
  juint f = jint_cast(value);
  juint rf = reverse_bits(f);
  assert(f == reverse_bits(rf), "can re-read same bits");
  write_int(rf);
}

void CompressedWriteStream::write_double(jdouble value) {
  juint h  = high(jlong_cast(value));
  juint l  = low( jlong_cast(value));
  juint rh = reverse_bits(h);
  juint rl = reverse_bits(l);
  assert(h == reverse_bits(rh), "can re-read same bits");
  assert(l == reverse_bits(rl), "can re-read same bits");
  write_int(rh);
  write_int(rl);
}

void CompressedWriteStream::write_long(jlong value) {
  write_signed_int(low(value));
  write_signed_int(high(value));
}


bool CompressedSparseDataReadStream::read_zero() {
  if (_buffer[_position] & (1 << (7 - _bit_pos))) {
    return 0; // not a zero data
  }
  if (++_bit_pos == 8) {
    _position++;
    _bit_pos = 0;
  }
  return 1;
}

uint8_t CompressedSparseDataReadStream::read_byte_impl() {
  if (_bit_pos == 0) {
    return _buffer[_position++];
  }
  uint8_t b1 = _buffer[_position] << _bit_pos;
  uint8_t b2 = _buffer[++_position] >> (8 - _bit_pos);
  return b1 | b2;
}

jint CompressedSparseDataReadStream::read_int() {
  if (read_zero()) {
    return 0;
  }
  // integer value encoded as a sequence of 1 to 5 bytes
  // - the most frequent case (0 < x < 64) is encoded in one byte
  // - the payload of the first byte is 6 bits, the payload of the following bytes is 7 bits
  // - the most significant bit in the first byte is occupied by a zero flag
  // - each byte has a bit indicating whether it is the last byte in the sequence
  //
  //       value | byte0    | byte1    | byte2    | byte3    | byte4
  //  -----------+----------+----------+----------+----------+----------
  //           0 | 0        |          |          |          |
  //           1 | 10000001 |          |          |          |
  //           2 | 10000010 |          |          |          |
  //          63 | 10111111 |          |          |          |
  //          64 | 11000000 | 00000001 |          |          |
  //          65 | 11000001 | 00000001 |          |          |
  //        8191 | 11111111 | 01111111 |          |          |
  //        8192 | 11000000 | 10000000 | 00000001 |          |
  //        8193 | 11000001 | 10000000 | 00000001 |          |
  //     1048575 | 11111111 | 11111111 | 01111111 |          |
  //     1048576 | 11000000 | 10000000 | 10000000 | 00000001 |
  //  0xFFFFFFFF | 11111111 | 11111111 | 11111111 | 11111111 | 00011111
  //
  uint8_t b = read_byte_impl();
  juint result = b & 0x3f;
  for (int i = 0; (i == 0) ? (b & 0x40) : (b & 0x80); i++) {
    b = read_byte_impl();
    result |= ((b & 0x7f) << (6 + 7 * i));
  }
  return (jint)result;
}

void CompressedSparseDataWriteStream::write_zero() {
  if (_bit_pos == 0) {
    _buffer[_position] = 0;
  }
  _bit_pos++;
  if (_bit_pos == 8) {
    _position++;
    if (_position >= _size) {
      grow();
    }
    _buffer[_position] = 0;
    _bit_pos = 0;
  }
}

void CompressedSparseDataWriteStream::write_byte_impl(uint8_t b) {
  if (_bit_pos == 0) {
    _buffer[_position] = b;
  } else {
    _buffer[_position] |= (b >> _bit_pos);
  }
  _position++;
  if (_position >= _size) {
    grow();
  }
  if (_bit_pos > 0) {
    _buffer[_position] = (b << (8 - _bit_pos));
  }
}

// see CompressedSparseDataReadStream::read_int for a description of the encoding scheme
void CompressedSparseDataWriteStream::write_int(juint val) {
  if (val == 0) {
    write_zero();
    return;
  }
  int bit7 = 0x80; // first byte upper bit is set to indicate a value is not zero
  juint next = val >> 6;
  int bit6 = (next != 0) ? 0x40 : 0; // bit indicating a last byte
  write_byte_impl(bit7 | bit6 | (val & 0x3f));
  while (next != 0) {
    bit7 = (next >> 7) ? 0x80 : 0; // bit indicating a last byte
    write_byte_impl(bit7 | (next & 0x7f));
    next >>= 7;
  }
}

void CompressedSparseDataWriteStream::grow() {
  int nsize = _size * 2;
  assert(nsize > 0, "debug data size must not exceed MAX_INT");
  assert(nsize > _position, "sanity");
  u_char* _new_buffer = NEW_RESOURCE_ARRAY(u_char, nsize);
  memcpy(_new_buffer, _buffer, _position);
  _buffer = _new_buffer;
  _size   = nsize;
}
