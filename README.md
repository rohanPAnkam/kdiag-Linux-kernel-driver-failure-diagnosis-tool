# kdiag — Linux kernel/driver failure diagnosis tool

AI-assisted root-cause analysis for Linux kernel/PCIe/DMA/V4L2/GStreamer
driver failures. Takes kernel + hardware telemetry, reconstructs the
causal chain a captured frame should have gone through, and identifies
where it broke.

## Implementation Status

Data collection, event correlation, deterministic diagnosis, LLM explanation,
and the evaluation harness are implemented. Automated tests use synthetic
inputs; live camera-hardware collection and a real provider API call have not
been validated here.

## Prerequisites

- C++17 compiler
- CMake 3.16 or newer
- libcurl development files
- Network access on first configure if GoogleTest is not installed (CMake
  fetches it with FetchContent)

Build and tests work on macOS and Linux. **Live collection requires Linux**
because it reads `/proc`, `/sys`, `dmesg`, and PCI device information. Build on
your development machine, then run collection on the Linux camera host.
For collection, install `pciutils` for `lspci`; `journalctl` is used when
systemd-journald is available. Missing commands or permissions are recorded as
collector errors when possible.

## Build and Test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The build creates `build/kdiag` and `build/kdiag_tests`. The 88 tests use
synthetic input and mocked transports; they do not access camera hardware or
make network requests. Run the deterministic ten-scenario benchmark with:

```sh
./build/kdiag evaluate
```

To build without GoogleTest, configure with
`cmake -S . -B build -DKDIAG_BUILD_TESTS=OFF`.

## End-to-End Workflow

### 1. Find the device on Linux

On the Linux machine connected to the camera, identify its PCI address and
bound driver:

```sh
lspci -Dnnk
```

Use the device BDF (for example, `0000:03:00.0`) and the actual kernel module
name shown as `Kernel driver in use`.

### 2. Collect a capture

Reproduce the issue, then collect promptly so the logs and hardware snapshots
cover the failure:

```sh
sudo ./build/kdiag collect \
  --device 0000:03:00.0 \
  --driver driver_name \
  --output ./capture \
  --since "-10 min"
```

Replace the example BDF and driver with values from your system. `--since`
filters `journalctl -k`; the collector also reads `dmesg`, `/proc/interrupts`,
`/sys/class/video4linux`, and `lspci -vvv`. The command writes the normalized
event stream to `./capture/events.json`. Root is recommended for complete
kernel and PCI data. Collector failures are recorded in the event file where
possible rather than aborting the whole run.

### 3. Correlate and diagnose

Run these on the capture file (they do not need root):

```sh
./build/kdiag correlate --input ./capture/events.json
./build/kdiag diagnose --input ./capture/events.json
```

`correlate` shows the per-buffer timeline and first reported gap. `diagnose`
applies deterministic rules and prints confidence-ranked hypotheses, raw
evidence citations, and suggested investigation steps. Add `--verbose` to
include low-confidence patterns:

```sh
./build/kdiag diagnose --input ./capture/events.json --verbose
```

### 4. Explain the report

Without an API key, `explain` makes **no network request** and prints a local
summary of the rule output. The diagram is optional:

```sh
./build/kdiag explain --input ./capture/events.json --show-diagram
```

To request an Anthropic explanation, configure the key in your shell or secret
manager; do not put it in command-line arguments or commit it to the repository:

```sh
# Set ANTHROPIC_API_KEY in this shell or through your secret manager first.
./build/kdiag explain --input ./capture/events.json --show-diagram --deep-dive
```

Optionally set `KDIAG_LLM_MODEL` or `KDIAG_LLM_ENDPOINT` to override the
default model or use a compatible endpoint. When remote mode is enabled, the
selected device metadata, diagnosis hypotheses, and cited raw event lines are
sent to that endpoint. Review the capture for sensitive information first.
The response is explanatory text, not verified diagnosis; use the deterministic
report and original logs as the evidence source.

### 5. Run the synthetic evaluation

```sh
./build/kdiag evaluate
```

The benchmark covers ten synthetic scenarios, including clean capture as a
negative test. It reports root-cause matches separately from localization;
some scenarios are marked “not assessed” because the correlator reports only
the gap after the highest reached stage.

For command options, run `./build/kdiag --help`.

## Event Format

Each event preserves a normalized timestamp, source, component, event tag,
message, raw source text, and structured fields. Example:

```json
{
  "timestamp_ns": 10125000000,
  "source": "dmesg",
  "component": "dma",
  "event": "dma_descriptor",
  "message": "descriptor submitted for buffer 1832",
  "raw": "[  10.125000] driver_name 0000:03:00.0: descriptor submitted for buffer 1832",
  "fields": {}
}
```

The schema and serialization code live in `include/kdiag/event.hpp`.

## Diagnosis Limits

- Findings are hypotheses, not confirmed root causes. Confidence is qualitative,
  not a statistical probability.
- The classifier depends on log wording and driver instrumentation. Missing
  events may mean “not logged” or “outside the capture window.”
- `/proc/interrupts` is captured once, so kdiag cannot determine whether a
  counter changed after a descriptor was submitted.
- PCI and interrupt data are snapshots, not points on the buffer timeline.
- `dmesg` uses boot-relative time while `journalctl` uses wall-clock time;
  clock-domain reconciliation is not implemented. `dmesg` events are the
  reliable source for timeline ordering at present.
- A GStreamer log collector is not included. GStreamer-stage rules only work
  when suitable GStreamer events are present in the input event stream.
- No live hardware capture or real LLM API call is covered by the automated
  tests; validate on the target Linux system before relying on results.

## Architecture

```
main.cpp
  ├── collect_command.cpp    (collection entry point)
  │     └── collectors/*     (dmesg, /proc/interrupts, sysfs V4L2, lspci, journalctl)
  │           └── kernel_line_classifier.{hpp,cpp}   (Component tagging: dma/irq/pci/v4l2/driver)
  ├── correlate_command.cpp   (correlation entry point)
  │     └── correlator.{hpp,cpp}
  │           ├── stage.{hpp,cpp}         (pipeline stage classifier)
  │           └── event.hpp               (Event and CollectionRun schema)
  ├── diagnose_command.cpp    (deterministic diagnosis reports)
  │     └── diagnosis_rules.{hpp,cpp}     (ranked evidence-based hypotheses)
  ├── explain_command.cpp     (explanation + timeline)
  │     ├── llm_explainer.{hpp,cpp}       (Anthropic API and local fallback)
  │     └── ui_renderer.{hpp,cpp}         (ASCII pipeline timeline)
  └── evaluate_command.cpp    (benchmark CLI)
        └── evaluator.{hpp,cpp}           (ten synthetic scenarios)
```

### Design Approach

- **Strategy with dependency injection for system commands.** Collectors use
  the `CommandRunner` interface instead of calling process APIs directly.
  `RealCommandRunner` executes commands in production, while tests inject a
  fake runner with canned results. Commands are passed as argument vectors,
  so values are not interpreted by a shell.
- **Polymorphic collector interface.** Each source implements `Collector` and
  owns its parsing and expected-failure behavior. `RunCollectCommand` composes
  the concrete collectors, invokes them, and merges their events; adding a
  source does not require changing the other collectors.
- **Rule-based diagnosis policies.** `DiagnosisRule` pairs a matcher with a
  hypothesis generator and explicit scope metadata. The engine applies these
  policies and deduplicates collection-wide findings by stable rule ID rather
  than inferring behavior from display text.
- **Shared loading boundary.** `LoadCollectionRun` centralizes reading,
  parsing, and validating the normalized collection file for commands that
  consume it, keeping that input contract consistent.

These are focused patterns and boundaries rather than a framework: command
execution is the main Strategy seam, and the CLI command implementations act
as composition roots for their dependencies. There is no global service
locator or Singleton; tests can substitute dependencies explicitly.

Design decisions worth knowing about before extending this:

- **Component vs. Stage are separate classification passes.** Component
  answers "which subsystem" for coarse filtering. Stage answers "which point
  in one buffer's lifecycle" for gap detection. A line can have a Component
  with no Stage — most dmesg noise does — and that's expected.
- **Every collector takes a `CommandRunner&`**, never constructs one, so
  every collector is unit-testable with canned output
  (`tests/fake_command_runner.hpp`) instead of requiring root, a running
  systemd-journald, or actual PCI hardware in CI.
- **System commands use argument vectors rather than shell strings.** Variable
  values such as the PCI BDF and journal time range are passed as individual
  arguments, avoiding shell interpretation of user input.
- **Driver log matching follows `--driver`.** The selected module name is
  passed into both kernel-log classifiers; when omitted, the anonymized
  `driver_name` fixture marker is used rather than claiming a specific module.
- **Snapshot vs. timeline events.** `/proc/interrupts` and `lspci`
  describe cumulative/current state, not a point in time. Those events
  carry `timestamp_ns == kSnapshotTimestampNs` and `fields.snapshot ==
  true`, and are excluded from stage/timeline correlation — treating a
  cumulative counter as a causal-chain event would be exactly the kind
  of invented precision this project is supposed to avoid.
- **Clock domains are not silently mixed.** `dmesg` timestamps are
  boot-relative monotonic seconds; `journalctl` timestamps are wall
  clock, converted to epoch-ns and tagged
  `fields.clock_domain == "wall_clock_ns"`. Reconciling the two domains
  (sampling a boottime/walltime offset once per collection run) remains
  unsolved — currently only `dmesg`-sourced events participate reliably
  in one buffer's chain.
- **A missing stage is a candidate, not a confirmed root cause.** The
  correlator reports the first pipeline stage that never appeared after
  the highest stage it did see. It cannot yet distinguish "this driver
  never logs a device_init line on the happy path" from "device_init
  genuinely never happened." The diagnosis rules report selected observed
  patterns as candidates, but per-driver expected-instrumentation
  profiles are still needed to interpret other missing stages reliably.

## Non-goals (explicitly out of scope per the original plan)

Custom kernel, custom QEMU hardware models, a full GStreamer
replacement, a GPU framework, distributed architecture, a web SaaS,
Kubernetes deployment, an elaborate frontend, or supporting 100
different drivers. This is a focused CLI tool for one class of failure
on the systems you actually maintain (`driver_name` / custom PCI camera
driver), not a general observability platform.
