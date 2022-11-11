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
 */

#include "precompiled.hpp"
#include "classfile/classPrinter.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/javaThread.hpp"
#include "utilities/ostream.hpp"

#include "unittest.hpp"

TEST_VM(ClassPrinter, print_classes) {
  JavaThread* THREAD = JavaThread::current();
  ThreadInVMfromNative invm(THREAD);
  ResourceMark rm;

  stringStream ss;
  ClassPrinter::print_classes("java/lang/Object", 0x03, &ss);
  const char* output = ss.freeze();

  ASSERT_TRUE(strstr(output, "class java/lang/Object loader data:") != NULL) << "must find java/lang/Object";
  ASSERT_TRUE(strstr(output, "method wait : (J)V") != NULL) << "must find java/lang/Object::wait";
  ASSERT_TRUE(strstr(output, "method finalize : ()V\n   0 return") != NULL) << "must find java/lang/Object::finalize and disasm";
}

TEST_VM(ClassPrinter, print_methods) {
  JavaThread* THREAD = JavaThread::current();
  ThreadInVMfromNative invm(THREAD);
  ResourceMark rm;

  stringStream s1;
  ClassPrinter::print_methods("*ang/Object*", "wait", 0x1, &s1);
  const char* o1 = s1.freeze();
  ASSERT_TRUE(strstr(o1, "class java/lang/Object loader data:") != NULL) << "must find java/lang/Object";
  ASSERT_TRUE(strstr(o1, "method wait : (J)V")    != NULL) << "must find java/lang/Object::wait(long)";
  ASSERT_TRUE(strstr(o1, "method wait : ()V")     != NULL) << "must find java/lang/Object::wait()";
  ASSERT_TRUE(strstr(o1, "method finalize : ()V") == NULL) << "must not find java/lang/Object::finalize";

  stringStream s2;
  ClassPrinter::print_methods("j*ang/Object*", "wait:(*J*)V", 0x1, &s2);
  const char* o2 = s2.freeze();
  ASSERT_TRUE(strstr(o2, "class java/lang/Object loader data:") != NULL) << "must find java/lang/Object";
  ASSERT_TRUE(strstr(o2, "method wait : (J)V")  != NULL) << "must find java/lang/Object::wait(long)";
  ASSERT_TRUE(strstr(o2, "method wait : (JI)V") != NULL) << "must find java/lang/Object::wait(long,int)";
  ASSERT_TRUE(strstr(o2, "method wait : ()V")   == NULL) << "must not find java/lang/Object::wait()";
}
