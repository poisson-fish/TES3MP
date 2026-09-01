# ADR-0040: Phase 7 server composition and configuration

Status: **Accepted**

Date opened: 2026-09-01

Date approved: 2026-09-01

Decision owner: project owner

Needed by: Phase 7 Slice 7.1

## Decision questions

Where does the first dedicated-server composition root live, how does it drive
owned subsystems, what configuration and secret surface may it accept, and how
does startup and shutdown avoid partial externally visible state?

## Decision summary

The project owner approved Option A for all six decisions on 2026-09-01:

1. a thin `tes3mp_server` executable and testable composition library live under
   `apps/tes3mp-server`; core libraries remain composition-free;
2. one caller-owned thread deterministically pumps transport and later server
   work;
3. configuration is a strict bounded UTF-8 `key = value` file with a closed key
   set and no includes, interpolation, or duplicate/unknown keys;
4. the first file exposes only bind address, port, tick interval, disconnect
   grace, and join-password file path, with hard validation bounds;
5. the password is read only from a bounded dedicated file and is never inline,
   echoed, or retained in parsed configuration text; and
6. all input and dependencies validate before listening; termination requests
   an orderly stop that admits no new work, closes transport, and destroys
   dependencies in ownership order.

## Options and rationale

For each decision, Option A above is approved. Rejected alternatives were:

- putting composition inside `server_core`, which would reverse the owned port
  boundary;
- adding network/simulation worker threads and queues before workload evidence;
- adding a JSON dependency or using command-line-only configuration;
- exposing transport capacities, fixture internals, or tuning prematurely;
- storing an inline plaintext password; and
- listening before construction completes.

The chosen design is the smallest executable seam that preserves ADR-0014 and
ADR-0032. Single-thread pumping is an ownership rule, not a promise that later
server releases can never use bounded worker queues.

## Required configuration contract

The exact required keys are `bind_address`, `port`, `tick_interval_ms`,
`disconnect_grace_ms`, and `join_password_file`. Input is at most 4096 bytes;
each physical line is at most 512 bytes. Blank lines and lines whose first
non-space byte is `#` are ignored. Keys are ASCII and case-sensitive. Values are
trimmed UTF-8 text without quoting, escapes, inline comments, environment
expansion, or interpolation.

`bind_address` must be a numeric IPv4 or IPv6 listener address accepted by the
owned transport boundary. `port` is 1 through 65535. `tick_interval_ms` is 1
through 1000. `disconnect_grace_ms` is 0 through 600000. The password path is
nonempty and at most 1024 UTF-8 bytes. The password file must contain 1 through
the protocol hard maximum bytes; one terminal LF or CRLF is removed. Errors are
bounded stable categories plus line numbers and key names, never secret values.

## Acceptance tests and demo

1. `valid_minimal_config_parses_all_owned_values`
2. `empty_oversized_invalid_utf8_long_line_and_malformed_assignment_fail`
3. `unknown_duplicate_missing_and_out_of_range_values_fail`
4. `inline_secret_include_interpolation_and_inline_comment_are_not_language_features`
5. `password_file_is_bounded_trimmed_move_only_and_never_rendered`
6. `composition_constructs_before_listen_and_stops_once_in_order`
7. `listener_failure_returns_actionable_bounded_error_without_partial_runtime`
8. `termination_request_stops_admission_before_transport_shutdown`
9. `server_app_links_composition_ports_without changing core dependencies`

The implementation demo must show valid startup/stop, one invalid configuration
error, one absent/over-bound secret error, and injected listener failure. No
client authentication, join, player, cell, movement, or resume behavior is part
of Slice 7.1.

## Review triggers

Reopen this ADR before adding configuration keys or syntax, inline/environment/
remote secrets, reload, discovery, daemon/service behavior, multiple listeners,
worker threads, cross-thread queues, or a different shutdown contract. Phase 21
owns the final operator configuration and secret-source policy.

## Owner approval

Approved by the project owner on 2026-09-01: Option A for Decisions 1 through 6
and the acceptance/demo boundary above.
