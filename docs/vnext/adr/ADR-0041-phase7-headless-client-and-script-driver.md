# ADR-0041: Phase 7 headless client and script driver

Status: **Accepted**

Date opened: 2026-09-01

Date approved: 2026-09-01

Decision owner: project owner

Needed by: Phase 7 Slice 7.2

## Decision summary

The project owner approved Option A for all six decisions on 2026-09-01:

1. `tes3mp_client_session` gains a caller-pumped headless façade around the
   existing state machine and owned transport API;
2. it borrows one caller-owned `TransportRuntime`, owns at most one attempt and
   connection, and creates no thread or selected-library object;
3. callers use bounded typed commands and polled typed events/snapshots, never
   callbacks, packet buffers, or mutable state views;
4. deterministic `ScriptedFakeClient` orchestration lives in test support and
   consumes the production façade;
5. scripts are fixed-capacity typed steps, with no external parser yet; and
6. evidence uses bounded typed sequence/tick/reason entries with credential-free
   escaped NDJSON output.

Rejected alternatives were app-specific transport driving, per-session runtime
construction, callbacks, production scripting behavior, and an early JSON or
custom script dependency. This record adds no authentication, join, player,
cell, movement, or reconnect behavior.

On 2026-09-01 the project owner approved process-composition Option A: a thin
`tes3mp_headless_client` executable owns one transport runtime, credential
input, the fixed typed join script, bounded pumping, and credential-free NDJSON
evidence. Protocol and session behavior remain in the engine-independent
libraries. Test-only and existing-test-binary process entry points were
rejected.

## Acceptance tests

1. one session owns at most one attempt/connection and rejects duplicate starts;
2. unrelated transport events are ignored and matching encrypted establishment
   advances the existing state machine;
3. poll/runtime failure closes fail-closed without callbacks or threads;
4. typed session events and snapshots preserve existing state-machine results;
5. fixed-capacity scripts reject overflow before execution;
6. same typed script produces the same ordered timeline;
7. NDJSON is bounded, escaped, and contains no packet or credential bytes.

The owner demo must show connect establishment, a typed snapshot result, script
replay equality, capacity rejection, and transport failure closure.

## Review triggers

Reopen before adding worker threads, callback delivery, runtime ownership,
packet-facing public APIs, an external script language, unbounded timelines, or
credential/raw-payload logging.

## Owner approval

Approved by the project owner on 2026-09-01: Option A for Decisions 1 through 6
and the acceptance/demo boundary above.
