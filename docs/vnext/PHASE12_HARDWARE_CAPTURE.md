# Phase 12 hardware/content movement capture

Date: 2026-09-07

Status: **Desktop complete; PC-VR deferred until a later presentation gate**

No movement, correction, playback, animation, pose, authority, collision,
interest, resync, or compatibility value changed in this pass.

## Capture surface

`scripts/run_phase12_movement_capture.py` launches a production server and two
content-backed clients through a bounded localhost UDP relay. Desktop capture
uses a test-only five-second walk/turn/stop route and fails on incomplete role
evidence, a nonzero process exit, missing metrics, sink overflow, timeout, or
relay failure. PC-VR capture uses the same relay and production VR provider but
remains operator-driven so OpenXR tracking and hardware presentation are real.

The fixed profiles are:

- direct: no intentional impairment;
- jitter: alternating 45 ms and 5 ms delivery delays per direction;
- loss: every twentieth datagram dropped per direction;
- stall: 5 ms baseline delay and one 250 ms hold at datagram 40 per direction.

## Desktop result

Windows MSVC Release clients used licensed Morrowind, Tribunal, and Bloodmoon
content in `Balmora, Guild of Mages`. Values are maxima across both clients.

| Profile | Ack max | Stop ack max | Local correction | Snapshot age | Buffer | Extrapolation | Remote correction | Hard snaps |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Direct | 246.7 ms | 24.3 ms | 198,038 q | 986.9 ms | 4 | 100 ms | 11,703 q | 1 |
| Jitter | 198.3 ms | 108.9 ms | 194,856 q | 927.7 ms | 4 | 100 ms | 4,060 q | 1 |
| Loss | 205.1 ms | 66.2 ms | 198,038 q | 1,013.8 ms | 4 | 100 ms | 16,384 q | 1 |
| Stall | 193.2 ms | 26.5 ms | 198,038 q | 974.0 ms | 4 | 100 ms | 10,923 q | 1 |

Each row retained a complete metric set with no fixed-sink drops. The one hard
snap is initial remote placement. Snapshot age includes the intentional
one-second stop tail. Join/startup effects remain in command and local-correction
maxima. These values are observations, not proposed player-facing budgets.

## PC-VR gate

The maintained `vnext-vr` target contains the tested Phase 12 merge at
`223d5a74e9`. Its fresh RelWithDebInfo `openmw_vr` and `tes3mp_server` targets
link; adapter, movement-evidence, server-app, provenance, composition, boundary,
and replicated-actor tests pass.

With a headset connected, run each profile using `--platform pc-vr`, the merged
`openmw_vr.exe`, the desktop peer, the same content arguments, and an operator
timeout long enough to walk, turn, stop, and close both clients. Review the
desktop peer for walk/turn/idle animation, the stall for canonical fallback, and
the VR log for pose age/loss. Do not ratify budgets until all four summaries and
the visual review are complete.

The owner deferred this gate on 2026-09-07 while the headset charges. Revisit it
after the first Phase 13 actor desktop/VR presentation demo, or during Phase 22
stabilization at the latest.
