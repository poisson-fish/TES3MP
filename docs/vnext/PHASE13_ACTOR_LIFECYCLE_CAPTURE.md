# Phase 13 content-backed actor lifecycle capture

Date: 2026-09-07

Status: **Complete**

## Route

`scripts/run_phase13_actor_lifecycle_capture.py` launches one production
dedicated server and real OpenMW desktop clients with licensed Morrowind,
Tribunal, and Bloodmoon content. The server uses one manifest-bound wandering
actor; clients map prototype `1` to `ajira` locally.

The bounded route runs two clients through exact-cell leave and re-entry, then
runs four disconnect/resume cycles. After grace expiration it launches a new
client process with the same durable player credential and requests an
authenticated complete resync. Evidence fails closed on missing completion,
more than 128 records per client, identity/revision regression, divergent
same-revision samples, missing actor presentation, timeout, process failure, or
credential leakage.

## Result

- The two simultaneous clients shared 33 exact actor revisions; identity,
  server tick, and position matched for every shared sample.
- Actor identity stayed actor `1`, entity `9001`, prototype `1`.
- Revision checkpoints advanced `238 -> 244 -> 367 -> 374` across cell flow,
  reconnect, resync, grace expiration, and credential-authenticated rejoin.
- The moving actor disappeared and returned for the transitioning client while
  remaining continuous for the client that stayed in the interior.
- All four resumes received actor state. Both authenticated resyncs completed
  with actor state, and the relaunched process retained player/entity `3/3`.
- The retained summary is `build/phase13-actor-lifecycle/summary.json`; build
  evidence is intentionally not source-controlled.

## Defects closed by the proof

- Desktop automation now forwards actor snapshots to the production renderer
  and retains bounded canonical samples.
- Presentation waits for player and actor membership revisions to converge in
  either transport-lane order before recreating actors across a cell change.
- Resync replays player state at its canonical publication checkpoint tick,
  rather than manufacturing a contradictory same-revision snapshot.
- A pending resync completes on applied or identical complete baselines and
  waits for both player and negotiated actor lanes.
- Older desktop/movement capture configs now supply the server's required empty
  actor artifact.

No actor authority, gameplay, persistence, protocol schema, collision, AI, or
player-facing tuning changed.
