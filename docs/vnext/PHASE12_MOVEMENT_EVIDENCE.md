# Phase 12 movement evidence baseline

Date: 2026-09-06

Status: **Complete**

This behavior-neutral baseline exercises the production measurement seams with
one deterministic walk, turn, and stop route. Desktop and PC-VR use the same
semantic command and canonical root path; the PC-VR capture additionally drives
the optional pose tracker. No movement rate, correction, buffer, snap, pose, or
gameplay value changed.

## Measurement surface

- A fixed-capacity, identity-free client sink summarizes command
  acknowledgement, stop, local correction, remote snapshot/buffer/extrapolation/
  correction/snap, and pose age/loss samples. Sink rejection is ignored by the
  movement path.
- The OpenMW composition uses that bounded sink and writes aggregate summaries
  only when the process exits. It emits no player, session, entity, cell, or
  credential fields.
- The server command-intake pump records its existing due-tick lag through the
  injected typed observability sink, including zero/no-work pumps.
- Pose evidence is keyed internally by entity and authority epoch only to detect
  gaps; those keys never enter observations. Sources outside the current view
  are discarded.

## Deterministic capture

The repository-owned `tes3mp_movement_evidence_tests` executable runs both
platform labels through four reproducible schedules. Direct uses regular
delivery and a 20 ms command acknowledgement. Jitter alternates 18/52 ms
snapshot intervals and uses 85 ms acknowledgements. Loss omits snapshots 4 and
8 and uses 180 ms acknowledgements. Stall inserts one 250 ms snapshot gap and
uses 350 ms acknowledgements. Local offsets of 128/512/1,024/4,096 quanta make
the correction seam observable without changing presentation.

Desktop and PC-VR values are identical for every non-pose field:

| Profile | Ack / stop max | Local correction max | Remote age max | Buffer max | Extrapolation max | Remote correction max | Hard snaps | Tick lag max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Direct | 20 ms | 128 quanta | 116.7 ms | 4 | 50.0 ms | 0 | 1 | 2 ticks |
| Jitter | 85 ms | 512 quanta | 126.7 ms | 4 | 40.0 ms | 4,096 quanta | 1 | 2 ticks |
| Loss | 180 ms | 1,024 quanta | 116.7 ms | 4 | 50.0 ms | 0 | 1 | 2 ticks |
| Stall | 350 ms | 4,096 quanta | 233.3 ms | 4 | 100.0 ms | 4,096 quanta | 1 | 7 ticks |

The initial remote placement accounts for the one hard snap in each row. The
capture includes a short no-update tail, so remote and pose age maxima also
exercise staleness rather than representing steady-state network latency.

| PC-VR profile | Pose age max | Lost pose samples |
|---|---:|---:|
| Direct | 116.7 ms | 0 |
| Jitter | 126.7 ms | 0 |
| Loss | 116.7 ms | 2 |
| Stall | 233.3 ms | 0 |

These are fixture baselines, not approved player-facing budgets. Hardware- and
content-backed desktop/PC-VR capture remains required to tune or ratify a
production policy. The capture does show that stalls reach the current 100 ms
remote extrapolation ceiling, loss is visible as pose sequence gaps, and the
shared platform-neutral locomotion path produces equal desktop and PC-VR
evidence.

## Verification

- `openmw_tes3mp_adapter_tests`, `tes3mp_observability_tests`,
  `tes3mp_server_command_intake_tests`, and `tes3mp_server_app_tests` pass.
- `tes3mp_movement_evidence_tests` passes and emits all eight platform/profile
  records.
- The MSVC Release `openmw.exe` target builds and links with the production
  GameNetworkingSockets path; the networking-enabled `tes3mp_server.exe` target
  also builds and links.
- All 145 repository Python tests and diff hygiene pass.
