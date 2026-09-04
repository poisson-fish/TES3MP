# ADR-0055: Phase 9 optional VR pose capability and schema

Status: **Approved**

Date opened: 2026-09-03

Decision owner: project owner

Needed by: Phase 9, Slice 9.3

## Decision requested

Slice 9.3 needs a stable optional capability and bounded head/hand pose wire
shape. The decision must define direction, identity, recency, numeric form,
tracking absence, and framing without making pose canonical state or letting it
replace authoritative world snapshots in the current one-slot latest-wins
queue.

## Retained constraints

- Pose is ephemeral presentation data. It cannot move the authoritative root,
  prove reach, enter canonical checksums/replay/persistence, or become gameplay
  input.
- ADR-0022 requires a stable nonzero `u32` capability, immutable negotiation,
  ignored unknown optional IDs, and rejection of unsupported required IDs.
- ADR-0024 requires pose to use its own typed message kind and sample metadata,
  not the canonical world-snapshot root.
- ADR-0004 requires verifier-first FlatBuffers, owned values, explicit field
  IDs, bounded semantic conversion, and no generated views outside the codec.
- ADR-0037 currently provides one replaceable latest-wins slot per connection.
  Pose traffic cannot enter it until later keyed coalescing prevents pose and
  canonical snapshots from replacing one another.
- GDR-0004 and Phase 12 still own production movement, pose hardening, reach,
  interaction, and gameplay semantics.

## Scenarios

1. A desktop-only peer does not offer `vr_pose`; negotiation and existing state
   exchange remain unchanged.
2. Two capable peers negotiate `vr_pose`; capability presence permits the pose
   message kinds but does not identify either peer as VR.
3. An unknown optional capability is ignored. A peer requiring unsupported
   `vr_pose` is rejected by the existing handshake rule.
4. A client attempts to name another player or stale root authority. Later
   session routing rejects it; pose never mutates canonical state.
5. Head tracking is present while either hand is absent. The valid sample keeps
   the missing hand absent rather than using a zero transform.
6. A sample is oversized, out of root-relative range, zero-sequenced, malformed,
   or contradictory at one sequence. Decode/session validation fails without a
   partial value.
7. Samples reorder or duplicate. A strictly newer source sequence wins;
   identical duplicates are harmless and contradictory duplicates fail closed.
8. A client reconnects. Source session generation separates the new sequence
   space from stale presentation packets; no pose is restored or persisted.
9. Codec support lands before runtime routing. Production offers remain empty,
   and pose frames cannot enter transport queues until Slice 9.5 adds approved
   keyed coalescing and rate limits.

## Decision 1: capability identity and enablement

### Option A — capability ID 1, optional product use (recommended)

Reserve stable capability ID `1` as `vr_pose`. A client or server advertises it
only when its complete send/receive role is installed. Product composition never
requires it; capability absence preserves existing behavior. Field or message
presence cannot enable it.

### Option B — infer support from platform or message presence

This avoids a registry entry but violates negotiated capability rules, exposes
platform branching, and permits capability smuggling.

### Option C — require pose for VR clients

This makes a presentation feature block connection and prevents graceful
desktop/VR interoperability when tracking or pose rendering is unavailable.

## Decision 2: direction, framing, and queue isolation

### Option A — two typed roots in a presentation class (recommended)

Add `ClientVrPoseSample` (`T3VP`, kind `0x0300`) and
`ServerVrPoseSnapshot` (`T3VR`, kind `0x0301`) under a new
`PresentationSample` frame class with a 1 KiB payload ceiling. The client root
names source session/generation, root entity/authority epoch, and sample
sequence. The server root additionally names target session/generation and
source player identity. Neither root carries canonical revision, server tick,
command acknowledgement, platform identity, or opaque bytes.

Slice 9.3 adds framing and pure codecs only. Runtime dispatch and transport
mapping remain unavailable until Slice 9.5 adds a distinct keyed latest-wins
queue policy. This prevents pose from replacing canonical snapshots.

### Option B — one bidirectional pose root

This saves one schema but either trusts client-authored player identity or makes
direction-dependent fields optional. Both weaken spoofing and missing-field
validation.

### Option C — add pose to `LatestWinsSnapshot`

This is compact but labels presentation as canonical, couples its recency to
canonical revision, and lets the current single slot replace world state.

## Decision 3: provisional transform and tracking shape

### Option A — root-relative fixed values, required head, optional hands (recommended)

Each tracked transform contains a root-relative signed position offset in the
existing `1/1024` OpenMW-unit quanta and the existing three `Turn32` orientation
components. Every offset component is limited to plus or minus 1,048,576 quanta
(1,024 OpenMW units). The head is required; left and right hands are independently
optional. One frame contains exactly one sample and no collection.

This avoids floats, NaNs, quaternion normalization rules, and unbounded vectors.
It is explicitly a provisional presentation encoding. Phase 12 may add a new
capability/message version for hardened quaternion compression; stable IDs and
field meanings are not reused.

### Option B — fixed-point normalized quaternion

This better matches OpenXR and interpolation, but Slice 9.3 would need to choose
normalization, canonical sign, tolerance, conversion, and compression rules that
Phase 12 is meant to harden.

### Option C — binary32 position and quaternion

This maps directly to tracking APIs but adds non-finite, negative-zero,
normalization, cross-platform conversion, and golden-byte concerns.

## Decision 4: recency and lifetime

### Option A — nonzero source sequence scoped to session generation (recommended)

Use a nonzero `u64` sample sequence that increases within one source session
generation. Older samples are stale; equal sequences must be byte-equivalent.
Pose is never persisted or restored. Disconnect, observation loss, generation
change, or later Slice 9.5 timeout clears presentation state. Client timestamps
and tracking/render frame counters do not cross the wire.

### Option B — use `ServerTick`

Multiple tracking samples may occur within one simulation tick, while tracking
and network rates must remain independent of the 30 Hz canonical scheduler.

### Option C — use client wall-clock timestamps

Peer clocks are not comparable or trusted, and timestamps would create clock
sync, privacy, range, and replay rules without improving latest-wins ordering.

## Decision 5: unknown additive FlatBuffer fields

Implementation review found a conflict between proposed acceptance test 8,
which says unknown fields reject, and retained ADR-0004, which requires
additive optional-field evolution and current/previous-minor compatibility.

### Option A — preserve ADR-0004 additive compatibility (recommended)

Verified unknown additive table fields are ignored by an older codec. Unknown
frame classes, message kinds, file identifiers, enum/union discriminants, and
malformed fields still reject. Acceptance test 8 becomes: `all truncations,
bad identifiers, unknown classifications, and trailing bytes reject; verified
additive optional fields remain compatible`.

### Option B — reject every unknown table field

Inspect and pin exact vtable shapes after structural verification. This makes
the first pose schema closed, but violates ADR-0004's approved additive
evolution rule and prevents an older minor from reading a newer additive pose
message.

### Option C — issue a new capability and root for every additive field

Keep each pose table closed and version every addition with new identifiers.
This avoids unknown-field acceptance but expands negotiation and schema surface
for changes that ADR-0004 already permits within one protocol major.

## Recommendation

Decisions 1 through 4 use approved Option A. Approve Option A for Decision 5 so
ADR-0004 remains governing. This gives a small, deterministic, capability-gated
codec while keeping pose noncanonical and transport-disabled until queue
isolation and sampling policy are approved in Slice 9.5.

## Proposed acceptance tests

1. `vr_pose_capability_has_stable_id_one_and_is_optional`
2. `unknown_optional_capability_does_not_enable_pose`
3. `unsupported_required_vr_pose_rejects_before_authentication`
4. `pose_roots_have_distinct_directional_identifiers_and_message_kinds`
5. `presentation_frames_reject_over_1024_bytes`
6. `pose_round_trip_owns_root_relative_head_and_optional_hands`
7. `missing_head_zero_sequence_and_out_of_range_offsets_reject`
8. `all_truncations_bad_identifiers_unknown_fields_and_trailing_bytes_reject`
9. `equal_sequence_requires_identical_content_and_older_sequence_is_stale`
10. `generation_change_clears_pose_recency_without_persistence`
11. `pose_values_expose_no_canonical_revision_tick_ack_or_platform_type`
12. `pose_codec_compiles_without_openmw_openxr_renderer_or_transport_types`
13. `production_offers_remain_empty_and_runtime_pose_queueing_is_unavailable`
14. `golden_current_and_previous_minor_pose_capability_cases_pass`
15. `pose_codec_mutation_and_fuzz_corpus_pass`
16. `existing_protocol_session_transport_and_desktop_vr_build_gates_remain_green`

## Consequences and failure handling

- Slice 9.3 expands framing and protocol codec surface but not runtime traffic.
- The new presentation class reserves a small independent byte budget. Slice
  9.5 must amend transport/queue policy before mapping it to a lane.
- A malformed or unnegotiated pose frame is a protocol violation, but cannot
  mutate canonical or gameplay state.
- The provisional Euler form favors bounded interoperability over final tracking
  fidelity. Phase 12 must use a new versioned contract if it replaces it.
- Sequence exhaustion disables further pose samples for that generation; it
  never wraps.

## Review and replacement triggers

Reopen this decision if the 1 KiB budget is insufficient, root-relative range
cannot represent supported tracking spaces, Euler orientation cannot satisfy the
Phase 9 proof, keyed queue isolation requires a new transport lane, a pose value
is proposed for reach/gameplay validation, or Phase 12 replaces the provisional
encoding.

## Owner approval

Approved on 2026-09-03: Option A for Decisions 1 through 4. The original
acceptance tests were approved, but implementation found test 8 conflicts with
governing ADR-0004. Decision 5 and its corrected test 8 wording await explicit
owner approval. No choice is inferred. Implementation acceptance remains
separate.
