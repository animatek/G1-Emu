# Audio path review, 2026-10-19

Codex reviewed the shared audio bridge, plugin runner and its audio callback, native/JUCE
audio adapters, and DSP worker waiting. This is a focused review, not a complete audit of
the emulator, JIT, GUI or state restoration.

## Fixed

- **Invalid input conversion:** a NaN from an audio input survived interpolation and clamp,
  then reached a float-to-int conversion with undefined behavior. A small reproducer failed
  under UBSan's float-cast-overflow check. Both input entry points now silence non-finite
  samples and clip finite input to the normalized range before interpolation, also preventing
  subtraction overflow with extreme finite samples. The finite check inspects the float bits:
  Release uses `-Ofast`, which optimized away the initial `std::isfinite` implementation.
- **Device restart data race:** JUCE's `audioDeviceAboutToStart` writes the bridge's sample
  rate while the emulator output and DSP input threads may read it. The rate is now atomic.
  The buffering fields still belong to the device lifecycle: `setRate` must not overlap
  `render`. This does not reset or flush samples queued under the previous device rate.
- **Discard cost in the audio callback:** debt recovery and buffer trimming popped every
  discarded frame individually. A consumer-only batch skip now advances the read cursor
  once, bounded by the published write cursor. It allocates nothing and takes no locks.
  Trimming uses a snapshot, so a producer adding frames cannot extend the discard loop.

## Verification

- Release CMake build of `g1audiobridgetest` and `g1runnertest` succeeded.
- CTest: `g1audiobridgetest`, `g1runnertest`, `g1runnertest-expanded`: 3/3 passed.
  The runner checks bit-identical output between fixed and random host block sizes.
- The new test passed separately under ThreadSanitizer and UBSan with float-cast-overflow.
  It covers FIFO wrap, partial/full/empty skips, concurrent production and discards,
  malformed input through both entry points, and concurrent sample-rate changes.
- Local GCC `-O3` microbenchmark: 5,000 batches of 16,384 discarded frames, filling outside
  the timed region. Before: 7.10 and 7.30 microseconds per batch; after: 0.022 and 0.023
  microseconds. This measures discard only, not total emulator speed or energy consumption.
- No hardware, DAW, macOS or Windows run was performed. The full upstream test suite was
  not run; several upstream test executables were absent from the existing build.

## Further performance work

The DSP worker spin waits are still the main candidate for reducing CPU consumption.
They trade CPU for very frequent synchronization and already sleep when the host is idle;
changing their spin budget needs heavy-patch real-time measurements of CPU time, underruns
and audio equality on both models. An arbitrary shorter wait is not a verified improvement.

Each bridge reserves 768 KiB of sample storage (32,768 frames times six float channels),
even with no audio input. Smaller or optional queues could reduce per-instance memory,
but should follow tests of large host blocks, sample rates and restore bursts. The runner's
ahead distance is configurable while queue capacity is fixed; test that boundary before
reducing capacity. No memory-footprint reduction is claimed by this change.
