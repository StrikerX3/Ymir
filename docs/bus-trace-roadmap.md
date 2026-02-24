# Ymir Bus Trace — Priority TODO List (Post-First Real Trace)

## North star for this phase
Make the trace **accurate enough to analyze contention** (MSH2/SSH2/DMA timing interactions) and **practical enough to capture real workloads** without destroying emulation usability.

---

## P0 — Critical correctness blockers (do these first)

### 1) Fix SH-2 contention retry tracking (`retries` always 0)

#### Tasks
- [x] **Map the exact SH-2 wait/retry path**
- [x] **Define trace lifecycle for one bus access** (`BeginPendingBusAccess`, `OnPendingBusAccessRetry`, `CompletePendingBusAccess`, `CancelPendingBusAccess`)
- [x] **Set `tickFirstAttempt` only once**
- [x] **Increment `retries` on each blocked re-entry**
- [x] **Verify `tick_complete` semantics** (now emitted as `current_tick + serviceCycles` in completion path)
- [ ] **Audit all SH-2 access kinds**

#### Acceptance criteria
- [ ] Real trace shows **nonzero retry counts** in at least some records during MSH2 + DMA activity
- [ ] `tick_complete - tick_first_attempt` exceeds pure service time for at least some contended accesses
- [ ] No false inflation (retries doesn’t explode due to unrelated loops)

---

### 2) Fix SSH2 tracing (currently only 8 records)

#### Tasks
- [x] **Trace path parity audit: MSH2 vs SSH2** (same hooks in shared SH2 core)
- [x] **Check for master-only gating bugs** (none found; emit tags by `m_isMaster`)
- [x] **Check alternate execution paths** (shared SH-2 code path confirmed; no master-only bypass found)
- [x] **Tag master source explicitly at emit point**
- [x] **Validate SSH2 fetch + mem separately** (SSH2-only run captured 200K events)
- [x] **Test with a known scene/game boot where SSH2 should be active** (clean all-master gameplay window captured both MSH2/SSH2)

#### Acceptance criteria
- [x] SSH2 records are present beyond reset vectors in real traces
- [x] SSH2 contributes meaningful activity in game boot / active scenes
- [x] SSH2 events include correct kinds (`ifetch`, `read`, `write`, etc.)

---

### 3) Add focused validation tooling for the two critical fixes (fast feedback)

#### Tasks
- [x] Add a **small capture mode recipe** (documented env vars) for quick validation
- [x] Add a **trace summary output** (`bus-trace-convert --summary`) reporting master distribution and retry stats
- [ ] Add **regression checks** (manual/scripted)

#### Acceptance criteria
- [x] You can validate fixes in minutes without multi-GB captures
- [x] Failures are obvious from summary output (not manual JSON inspection)

---

## P1 — Make the trace usable in practice (performance + volume controls)

### 4) Tighten capture strategy so real-game traces are manageable

#### Tasks
- [x] **Standardize recommended capture presets** (see section below)
- [x] Add **start-tick filter** (`YMIR_BUS_TRACE_AFTER_TICK`)
- [x] Add **stop-tick filter** (`YMIR_BUS_TRACE_UNTIL_TICK`)
- [x] Add **kind filter** (`YMIR_BUS_TRACE_KIND`)
- [x] Add **sample ratio** (`YMIR_BUS_TRACE_SAMPLE_RATIO`)

#### Acceptance criteria
- [x] Can capture targeted windows without gigantic output
- [x] Users can isolate SSH2/DMA/contention windows quickly

---

## Recommended quick presets

### quick sanity
```bash
YMIR_BUS_TRACE=1 \
YMIR_BUS_TRACE_FILE=bus_trace.bin \
YMIR_BUS_TRACE_MAX_RECORDS=200000 \
YMIR_BUS_TRACE_KIND=ALL
```

### ssh2-only debug
```bash
YMIR_BUS_TRACE=1 \
YMIR_BUS_TRACE_MASTER=SSH2 \
YMIR_BUS_TRACE_MAX_RECORDS=200000
```

### targeted DMA window
```bash
YMIR_BUS_TRACE=1 \
YMIR_BUS_TRACE_MASTER=DMA \
YMIR_BUS_TRACE_AFTER_TICK=5000000 \
YMIR_BUS_TRACE_UNTIL_TICK=7000000
```

### contention-focused sampled run
```bash
YMIR_BUS_TRACE=1 \
YMIR_BUS_TRACE_MAX_RECORDS=500000 \
YMIR_BUS_TRACE_SAMPLE_RATIO=4
```

Convert and summarize:
```bash
bus-trace-convert bus_trace.bin bus_trace.jsonl
bus-trace-convert --summary bus_trace.bin
```


## Latest trace findings

- SSH2 tracing is functioning; early “missing SSH2” observations were capture-window/cap artifacts when MSH2 filled the record cap first.
- DMA tracing is situational (present in boot traces, absent in some gameplay windows), matching game behavior rather than instrumentation failure.
- `retries` remains zero across all provided runs because `IsBusWait()` did not report stalls in tested scenarios; this indicates the current Ymir timing model is not producing master-contention waits in those paths.
- `YMIR_BUS_TRACE_AFTER_TICK` and `YMIR_BUS_TRACE_MASTER` filters were validated; note that env vars persist across terminal sessions and can unintentionally constrain later captures.

### Blocked P0 acceptance items (model-dependent)

The remaining P0 retry acceptance checks require contention signals from emulator timing (`IsBusWait()==true`) during SH-2 memory accesses. Without that behavior in the current model/workload, trace-side instrumentation cannot create non-zero retries.
