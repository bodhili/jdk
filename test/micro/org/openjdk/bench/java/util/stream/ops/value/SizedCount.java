/*
 * Copyright (c) 2021, Oracle and/or its affiliates. All rights reserved.
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
package org.openjdk.bench.java.util.stream.ops.value;

import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Fork;
import org.openjdk.jmh.annotations.Measurement;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.OutputTimeUnit;
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Scope;
import org.openjdk.jmh.annotations.Setup;
import org.openjdk.jmh.annotations.State;
import org.openjdk.jmh.annotations.Warmup;

import java.util.concurrent.TimeUnit;
import java.util.stream.DoubleStream;
import java.util.stream.IntStream;
import java.util.stream.LongStream;
import java.util.stream.Stream;

/**
 * Benchmark for count operation in sized streams.
 */
@Fork(3)
@Warmup(iterations = 5, time = 1, timeUnit = TimeUnit.SECONDS)
@Measurement(iterations = 5, time = 1, timeUnit = TimeUnit.SECONDS)
@BenchmarkMode(Mode.AverageTime)
@OutputTimeUnit(TimeUnit.NANOSECONDS)
@State(Scope.Thread)
public class SizedCount {
    @Param("10000")
    private int size;

    @Param({"true", "false"})
    private boolean polluteTypeProfile;

    @Setup
    public void setup() {
        if (!polluteTypeProfile) return;
        for(int i=0; i<10000; i++) {
            IntStream.empty().skip(1).count();
            LongStream.empty().skip(1).count();
            DoubleStream.empty().skip(1).count();
            Stream.empty().skip(1).count();
        }
    }

    @Benchmark
    public long count0() {
        return IntStream.range(0, size)
            .count();
    }

    @Benchmark
    public long count2() {
        return IntStream.range(0, size)
            .map(x -> x)
            .map(x -> x)
            .count();
    }

    @Benchmark
    public long count4() {
        return IntStream.range(0, size)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .count();
    }

    @Benchmark
    public long count6() {
        return IntStream.range(0, size)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .count();
    }

    @Benchmark
    public long count8() {
        return IntStream.range(0, size)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .count();
    }

    @Benchmark
    public long count10() {
        return IntStream.range(0, size)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .count();
    }

    @Benchmark
    public long count10Skip() {
        return IntStream.range(0, size)
            .skip(1)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .map(x -> x)
            .count();
    }
}
