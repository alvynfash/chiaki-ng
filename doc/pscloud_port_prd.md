# PS Cloud Core Port PRD Tracker

Last updated: 2026-05-10 (runtime realtime audio sink contract slice)
Owner: core porting workstream (`chiaki-ng`)
Status: Active

## Goal

Reach behavioral parity with base Chiaki for PS Cloud core behavior while exposing
incremental, ABI-safe runtime contracts that Flutter/deckstation can consume.

## Scope

- In scope:
  - headless core runtime contract
  - override and sanity APIs
  - capabilities negotiation
  - policy override transport into resolved connect info
- Out of scope (for this tracker slice):
  - media texture/audio rendering implementation details
  - GUI polish

## Status Snapshot (No Overstatement)

- Completed and verification-backed:
  - ABI-safe runtime/headless capability and recovery contract surfaces.
  - Unit coverage for the implemented contract slices.
- Still in progress (outside this core-complete claim):
  - host-side live playback stabilization and long-run media-plane behavior in
    the Flutter embed path.
  - full live-stream E3/loss behavioral validation in host app runs.

## Current Porting Status (Strict)

- Done (verification surface):
  - completed-only vs full verification script modes in `pscloud-core-verify.sh`
  - static checks for landed recovery config/status/auto-recover surfaces
  - optional full-mode static checks for E2 gate + recovery sequence report symbols
- In progress:
  - live stream behavior validation in host app runs (outside this script scope)

## Phases

1. Parity-safe headless runtime surface
2. Runtime introspection and compatibility contract
3. Media-plane embed contract and bootstrap
4. Flutter integration parity validation

## Done

- Added media-plane E1 bootstrap endpoint:
  - `chiaki_media_capabilities_size`
  - `chiaki_media_capabilities_init`
  - `chiaki_media_capabilities`
  - `chiaki_media_capabilities_compat`
  - versioned media contract fields (`media_capabilities_version`, `media_event_schema_version`)
  - unit coverage for contract shape and default flags
- Added media-plane E2 API scaffolding (no-op behavior):
  - `chiaki_media_session_create`
  - `chiaki_media_session_destroy`
  - `chiaki_media_session_start_cloud_strings` (gated by `CHIAKI_MEDIA_E2_ENABLE`)
  - `chiaki_media_session_stop`
  - unit coverage for lifecycle and scaffold behavior
- Added E2 gated real start wiring:
  - gate off: parity-safe `CHIAKI_ERR_UNINITIALIZED`
  - gate on: uses real headless create/start and stop/join/destroy lifecycle
- Added media-session diagnostics contract:
  - `chiaki_media_session_get_stats`
  - `chiaki_media_session_wait_for_video_frame`
  - `chiaki_media_session_wait_for_event`
  - `chiaki_media_session_wait_for_readiness`
  - explicit media-session lifecycle state in stats
  - tracks started state + video/audio/event callback counts + last event type
- API version bumped to `36` for current runtime/media-contract slice.
- Expanded media readiness diagnostics:
  - lifecycle state is included in readiness reports
  - event counters now track `ready/stopped/error` events
  - last media-event monotonic timestamp is exposed for host-side health timelines
- Added ABI-safe media diagnostics helpers:
  - stats/readiness `*_size` and `*_init`
  - stats/readiness compat-copy APIs for struct-size tolerant host bindings
- Added host-friendly media health API:
  - summarized health state (`idle/starting/ready/degraded/terminal`)
  - direct mapping from readiness/session state to reduce host-side policy duplication
  - compat-copy helper for struct-size tolerant bindings
- Added policy-based health evaluator:
  - default health policy initializer in core
  - explicit readiness->health evaluation API for host-side deterministic behavior
  - threshold tuning support for degraded/terminal transitions
- Added session-level policy-aware health API variants:
  - `chiaki_media_session_get_health_report_with_policy`
  - `chiaki_media_session_get_health_report_with_policy_compat`
- Expanded media capabilities contract (`media_capabilities_version = 2`) to expose
  explicit support flags for session lifecycle + stats/readiness/health contracts.
- Expanded media capabilities contract (`media_capabilities_version = 3`) to expose
  policy-aware session health-report query support.
- Added health-policy defaults discovery API:
  - `chiaki_media_health_policy_size`
  - `chiaki_media_health_policy_defaults`
  - `chiaki_media_health_policy_defaults_compat`
- Expanded media capabilities contract (`media_capabilities_version = 4`) to expose
  health-policy defaults query support.
- Added named health-policy profile API:
  - `chiaki_media_health_policy_from_profile`
  - `chiaki_media_health_policy_from_profile_compat`
- Expanded media capabilities contract (`media_capabilities_version = 5`) to expose
  health-policy profile query support.
- Added session health-by-profile API:
  - `chiaki_media_session_get_health_report_with_profile`
  - `chiaki_media_session_get_health_report_with_profile_compat`
- Expanded media capabilities contract (`media_capabilities_version = 6`) to expose
  session health-by-profile query support.
- Added health-policy profile catalog API:
  - `chiaki_media_health_policy_profile_count`
  - `chiaki_media_health_policy_profile_info`
  - `chiaki_media_health_policy_profile_info_compat`
- Expanded media capabilities contract (`media_capabilities_version = 7`) to expose
  profile-catalog query support.
- Extended profile catalog metadata with stable key/label fields.
- Expanded media capabilities contract (`media_capabilities_version = 8`) to expose
  profile-metadata availability.
- Added profile key lookup API:
  - `chiaki_media_health_policy_profile_from_key`
  - `chiaki_media_health_policy_profile_from_key_compat`
- Expanded media capabilities contract (`media_capabilities_version = 9`) to expose
  profile key-lookup support.
- Added reverse profile-key mapping and key-based session health API:
  - `chiaki_media_health_policy_profile_key_from_profile`
  - `chiaki_media_session_get_health_report_with_profile_key`
  - `chiaki_media_session_get_health_report_with_profile_key_compat`
- Expanded media capabilities contract (`media_capabilities_version = 10`) to expose
  key-based session health query support.
- Added profile-key normalization API:
  - `chiaki_media_health_policy_profile_key_normalize`
  - `chiaki_media_health_policy_profile_key_normalize_compat`
- Expanded media capabilities contract (`media_capabilities_version = 11`) to expose
  key-normalization support.
- Added profile-resolution API:
  - `chiaki_media_health_policy_profile_resolution_size`
  - `chiaki_media_health_policy_profile_resolve`
  - `chiaki_media_health_policy_profile_resolve_compat`
- Expanded media capabilities contract (`media_capabilities_version = 12`) to expose
  profile-resolution support.
- Added media capability struct-size negotiation fields:
  - `min_session_stats_size`
  - `min_readiness_report_size`
  - `min_health_report_size`
  - `min_health_policy_size`
  - `min_profile_info_size`
  - `min_profile_resolution_size`
- Expanded media capabilities contract (`media_capabilities_version = 13`) to expose
  minimum struct sizes for ABI-safe host buffer allocation.
- Added consolidated diagnostics snapshot API:
  - `chiaki_media_session_diagnostics_snapshot_size`
  - `chiaki_media_session_diagnostics_snapshot_init`
  - `chiaki_media_session_get_diagnostics_snapshot`
  - `chiaki_media_session_get_diagnostics_snapshot_compat`
- Expanded media capabilities contract (`media_capabilities_version = 14`) to expose
  diagnostics snapshot support and snapshot struct-size negotiation.
- Added policy/profile/profile-key diagnostics snapshot variants:
  - `chiaki_media_session_get_diagnostics_snapshot_with_policy`
  - `chiaki_media_session_get_diagnostics_snapshot_with_profile`
  - `chiaki_media_session_get_diagnostics_snapshot_with_profile_key`
- Expanded media capabilities contract (`media_capabilities_version = 15`) to expose
  diagnostics snapshot policy/profile/profile-key support.
- Added ABI-safe compat variants for diagnostics snapshot policy/profile/profile-key APIs:
  - `chiaki_media_session_get_diagnostics_snapshot_with_policy_compat`
  - `chiaki_media_session_get_diagnostics_snapshot_with_profile_compat`
  - `chiaki_media_session_get_diagnostics_snapshot_with_profile_key_compat`
- Expanded media capabilities contract (`media_capabilities_version = 16`) to expose
  compat support for diagnostics snapshot policy/profile/profile-key queries.
- Added runtime policy override model:
  - `video_profile_auto_downgrade`
  - `enable_idr_on_fec_failure`
  - `packet_loss_max`
- Added policy validation for `packet_loss_max` range `[0.0, 1.0]`.
- Applied policy overrides to resolved `ChiakiConnectInfo` in runtime paths.
- Extended runtime override set/get/patch with policy section support.
- Added runtime capabilities API set:
  - `chiaki_headless_runtime_capabilities_size`
  - `chiaki_headless_runtime_capabilities_init`
  - `chiaki_headless_runtime_get_capabilities`
  - `chiaki_headless_runtime_get_capabilities_compat`
- Expanded runtime capabilities contract with explicit runtime session lifecycle
  availability flags for host validation:
  - `supports_runtime_cloud_start_strings`
  - `supports_runtime_cloud_stop`
  - plus unit assertions in `test/headless.c` to prevent regressions.
- Added runtime media diagnostics snapshot API set:
  - `chiaki_headless_runtime_get_media_diagnostics_snapshot`
  - `chiaki_headless_runtime_get_media_diagnostics_snapshot_compat`
  - `chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key`
  - `chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key_compat`
- Added runtime recovery control API:
  - `chiaki_headless_runtime_request_idr`
- Added runtime-native realtime audio sink contract (opt-in, parity-safe):
  - `ChiakiHeadlessRuntimeAudioSinkConfig` with start/submit/stop callbacks
  - runtime setters/getters:
    - `chiaki_headless_runtime_set_audio_sink_config`
    - `chiaki_headless_runtime_get_audio_sink_config`
  - runtime sink diagnostics:
    - `chiaki_headless_runtime_get_audio_sink_diagnostics`
    - `chiaki_headless_runtime_get_audio_sink_diagnostics_compat`
  - explicit underrun reporting hook:
    - `chiaki_headless_runtime_audio_sink_report_underrun`
  - session-lifecycle integration:
    - sink start on first decoded audio readiness/frame path
    - sink stop on session stop/destroy
  - sink-disabled path remains behavior-compatible with prior callback-only flow
- Runtime capabilities now expose runtime diagnostics snapshot support/compat flags and
  `min_runtime_diagnostics_snapshot_size` for ABI-safe host buffers.
- Added runtime diagnostics-driven recovery decision API set:
  - `chiaki_headless_runtime_get_recovery_decision`
  - `chiaki_headless_runtime_get_recovery_decision_compat`
  - `chiaki_headless_runtime_get_recovery_decision_with_profile_key`
  - `chiaki_headless_runtime_get_recovery_decision_with_profile_key_compat`
- Runtime capabilities now expose recovery-decision support/compat flags and
  `min_runtime_recovery_decision_size` for ABI-safe host buffers.
- Added runtime recovery execution API set:
  - `chiaki_headless_runtime_apply_recovery_decision`
  - `chiaki_headless_runtime_apply_recovery_decision_compat`
  - `chiaki_headless_runtime_recover`
  - `chiaki_headless_runtime_recover_with_profile_key`
- Runtime capabilities now expose recovery-apply support/compat flags so hosts can
  negotiate one-step recovery loops.
- Added runtime recover-result API set:
  - `chiaki_headless_runtime_get_recover_result`
  - `chiaki_headless_runtime_get_recover_result_with_profile_key`
  - `chiaki_headless_runtime_get_recover_result_compat`
  - `chiaki_headless_runtime_get_recover_result_with_profile_key_compat`
- Runtime capabilities now expose recover-result support/compat flags and
  `min_runtime_recovery_result_size` for ABI-safe host allocations.
- Added tuned/stateful runtime recovery API set:
  - `chiaki_headless_runtime_recovery_tuning_size`
  - `chiaki_headless_runtime_recovery_tuning_init`
  - `chiaki_headless_runtime_recover_tuned`
  - `chiaki_headless_runtime_recover_with_profile_key_tuned`
  - `chiaki_headless_runtime_recover_with_profile_key_tuned_compat`
- Runtime capabilities now expose tuned-recovery support/compat flags and
  `min_runtime_recovery_tuning_size` for ABI-safe host allocations.
- Added runtime recovery status/reset API set:
  - `chiaki_headless_runtime_recovery_status_size`
  - `chiaki_headless_runtime_recovery_status_init`
  - `chiaki_headless_runtime_get_recovery_status`
  - `chiaki_headless_runtime_get_recovery_status_compat`
  - `chiaki_headless_runtime_reset_recovery_status`
- Runtime capabilities now expose recovery-status support/compat/reset flags and
  `min_runtime_recovery_status_size` for ABI-safe host allocations.
- Added core-managed runtime recovery config + auto-recover API set:
  - `chiaki_headless_runtime_recovery_config_size`
  - `chiaki_headless_runtime_recovery_config_init`
  - `chiaki_headless_runtime_set_recovery_config`
  - `chiaki_headless_runtime_get_recovery_config`
  - `chiaki_headless_runtime_set_recovery_config_compat`
  - `chiaki_headless_runtime_get_recovery_config_compat`
  - `chiaki_headless_runtime_set_recovery_profile_key`
  - `chiaki_headless_runtime_get_recovery_profile_key`
  - `chiaki_headless_runtime_recover_auto`
  - `chiaki_headless_runtime_recover_auto_compat`
  - `chiaki_headless_runtime_recover_auto_with_status`
  - `chiaki_headless_runtime_recover_auto_with_status_compat`
- Runtime capabilities now expose recovery-config and auto-recover support flags plus
  `min_runtime_recovery_config_size` for ABI-safe host allocations.
- Auto-recover now has a consolidated one-call host boundary variant that returns:
  - recover result (decision + applied action/apply error)
  - current recovery status (degraded streak / last IDR ts / runtime-active flag)
- Runtime capabilities now also expose:
  - recovery-config compat support
  - profile-key set/get support
  for enum-decoupled host integration.
- Added offline recovery simulation API set:
  - `chiaki_headless_runtime_simulate_recovery`
  - `chiaki_headless_runtime_simulate_recovery_with_profile_key`
  - `chiaki_headless_runtime_simulate_recovery_compat`
- Runtime capabilities now expose recovery-simulation support/compat flags for
  parity harnesses and host-side integration tests without live runtime sessions.
- Added simulation matrix unit coverage for:
  - degraded threshold gating
  - IDR cooldown gating
  - terminal stop behavior
  - degraded streak reset on healthy state
- Added sequence simulation API + unit coverage:
  - `chiaki_headless_runtime_simulate_recovery_sequence`
  - deterministic timeline-based checks for cooldown across consecutive degraded steps
- Added sequence simulation report API + coverage:
  - `chiaki_headless_runtime_simulate_recovery_sequence_report`
  - aggregate action counts + terminal/stop markers for parity tracking artifacts
- Added profile-key-native sequence simulation/report APIs + coverage:
  - `chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key`
  - `chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key`
  - enables parity fixtures to target normalized profile keys directly without
    assembling full runtime config structs in host harnesses
- Added ABI-safe compat variants for profile-key sequence/report simulation:
  - `chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_compat`
  - `chiaki_headless_runtime_simulate_recovery_sequence_report_with_profile_key_compat`
  - enables host FFI callers to pass truncated/forward-compatible buffers
    while keeping profile-key-native parity harness workflows
- Added lightweight health-step replay APIs for faster parity harness runs:

## Verification (this slice)

- `bash /Users/alvyn/Projects/Python/chiaki-ng/scripts/pscloud-core-verify.sh --mode completed-only --check-docs`
- `bash /Users/alvyn/Projects/Python/chiaki-ng/scripts/pscloud-core-verify.sh --mode full --check-docs`
  - `chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key`
  - `chiaki_headless_runtime_simulate_recovery_health_sequence_report_with_profile_key`
  - compat variants for both (`*_compat`) to keep ABI-safe host integrations
  - allows replaying health/error/readiness timelines without full diagnostics snapshots
- Expanded simulation report schema for richer parity artifacts:
  - first-action indices (`first_idr_step_index`, `first_none_step_index`)
  - per-health-state step aggregates (`healthy_step_count`, `degraded_step_count`, `terminal_step_count`)
  - applied across snapshot-sequence, profile-key-sequence, and health-step replay reports
- Runtime capabilities now expose ABI-safe minimum sizes for simulation structs:
  - step input/output + health-step input + simulation report
  - enables host FFI allocation/stride guards without hard-coded struct layouts
- Added reusable report-builder API from precomputed simulation outputs:
  - `chiaki_headless_runtime_recovery_simulation_report_from_outputs`
  - `chiaki_headless_runtime_recovery_simulation_report_from_outputs_compat`
  - all runtime simulation report paths now share the same aggregation logic to reduce drift
- Added single-pass sequence+report APIs for host efficiency:
  - `chiaki_headless_runtime_simulate_recovery_sequence_with_profile_key_with_report`
  - `chiaki_headless_runtime_simulate_recovery_health_sequence_with_profile_key_with_report`
  - compat variants for both; returns per-step outputs + final status + aggregate report in one call
- Added config-native single-pass sequence+report APIs for symmetry and simpler parity harnessing:
  - `chiaki_headless_runtime_simulate_recovery_sequence_with_report`
  - `chiaki_headless_runtime_simulate_recovery_sequence_with_report_compat`
  - keeps config-path host tests on one call (step outputs + final status + aggregate report)
    without profile-key indirection
- Added table-driven cross-path parity fixtures for runtime recovery simulation:
  - scenarios: aggressive degraded cooldown path, terminal stop path, steady-ready path
  - each scenario now runs through:
    - config single-pass sequence+report
    - profile-key single-pass sequence+report
    - health-step single-pass sequence+report
  - asserts report-level equivalence on key parity fields:
    - `idr_action_count`, `none_action_count`, `stop_action_count`
    - `first_idr_step_index`, `first_none_step_index`
    - health-state aggregates (`terminal_step_count`, `degraded_step_count`, `healthy_step_count`)
- Fixture-eval host contract coverage is staged:
  - test scaffolding expects Worker1 fixture-eval API/compat surfaces
  - once those symbols land in `headless.h`, this test block will add mixed pass/fail fixture-eval assertions plus tiny compat `api_version` checks
- Added deterministic auto-recover loop timeline APIs for detailed host tracing:
  - `chiaki_headless_runtime_simulate_recovery_auto_loop_timeline`
  - `chiaki_headless_runtime_simulate_recovery_auto_loop_timeline_compat`
  - consumes diagnostics-snapshot simulation inputs and returns per-step timeline records:
    - decision/applied action
    - degraded streak + last IDR monotonic timestamp
    - health state + event/error/ready counters
  - plus aggregate summary (action counts, first-action indices, health-state counts, final status)
  - runtime capabilities now expose support flags and ABI-safe min sizes for
    timeline step/summary structs
- Added runtime recovery parity smoke contract for compact host checks:
  - `chiaki_headless_runtime_recovery_parity_smoke`
  - `chiaki_headless_runtime_recovery_parity_smoke_compat`
  - returns aggregate scenario pass/fail counts and first failing index
  - runtime capabilities now expose smoke support flags and
    `min_runtime_recovery_parity_smoke_result_size` for ABI-safe host buffers
- Added canonical core baseline parity smoke suite API:
  - `chiaki_headless_runtime_recovery_parity_baseline_smoke`
  - `chiaki_headless_runtime_recovery_parity_baseline_smoke_compat`
  - provides host-callable, deterministic built-in core parity check without
    requiring host-defined scenario vectors
- Added baseline parity suite introspection APIs for host UX/discovery:
  - `chiaki_headless_runtime_recovery_parity_baseline_scenario_count`
  - `chiaki_headless_runtime_recovery_parity_baseline_scenario_count_compat`
  - `chiaki_headless_runtime_recovery_parity_baseline_scenario_label`
  - `chiaki_headless_runtime_recovery_parity_baseline_scenario_label_compat`
  - unit coverage now includes bounds checks and tiny-buffer compat behavior
    for both count and label lookup paths
- Added baseline parity execution-details APIs for deterministic host diagnostics:
  - `chiaki_headless_runtime_recovery_parity_baseline_execution_details`
  - `chiaki_headless_runtime_recovery_parity_baseline_execution_details_compat`
  - returns ordered per-scenario detail records with:
    - scenario index + label
    - pass/fail
    - key mismatch flags and expected/actual values for
      `idr_action_count`, `stop_action_count`, `none_action_count`,
      and (when enabled) `first_none_step_index`
  - plus aggregate smoke-runner result in the same call
- Added consolidated core diagnostics APIs for one-call host baseline+tuning visibility:
  - `chiaki_headless_runtime_recovery_core_diagnostics`
  - `chiaki_headless_runtime_recovery_core_diagnostics_compat`
  - composes baseline parity execution details + auto-loop timeline in one call:
    - returns per-scenario baseline details + aggregate baseline smoke result
    - returns per-step timeline records + aggregate timeline summary/final status
  - runtime capabilities now expose support flags and
    `min_runtime_recovery_core_diagnostics_summary_size` for ABI-safe host buffers
- Added host-facing parity smoke runner entrypoint that composes fixture-eval + smoke in one call:
  - `chiaki_headless_runtime_recovery_parity_smoke_runner`
  - `chiaki_headless_runtime_recovery_parity_smoke_runner_compat`
  - returns nested fixture/smoke outputs in one result payload for simpler host smoke wiring
  - runtime capabilities now expose runner support flags and
    `min_runtime_recovery_parity_smoke_runner_result_size` for ABI-safe host buffers
- Added deterministic fixture/replay export helper contracts for host baseline generation:
  - `chiaki_headless_runtime_recovery_parity_fixture_export`
  - `chiaki_headless_runtime_recovery_parity_fixture_export_compat`
  - converts replay step outputs + final status into:
    - parity expected marker (`expected_*` counts/indexes)
    - parity simulation report
  - runtime capabilities now expose fixture-export support flags for ABI-safe host negotiation
- Added compact runtime host-status contract for telemetry mapping:
  - `chiaki_headless_runtime_host_status_size`
  - `chiaki_headless_runtime_host_status_init`
  - `chiaki_headless_runtime_build_host_status`
  - `chiaki_headless_runtime_build_host_status_compat`
  - maps media health + recovery telemetry into one host payload
    (`health_state`, recommended/applied action, runtime active, streak/cooldown time,
    and event/error/ready counters)
  - runtime capabilities now expose host-status support flags and
    `min_runtime_host_status_size` for ABI-safe host buffer allocation
- Expanded deterministic parity fixture corpus and matrix:
  - reusable 10-step fixture corpus for health/error/ready/time timelines
  - new scenarios: cooldown boundary, terminal flap recovery transition,
    burst errors then ready stabilization, steady-ready long run
  - cross-path parity assertions now verify config/profile-key/health-step
    report equivalence over action counts, first-action indices, and
    health-state aggregates
- Added smoke API unit coverage:
  - all-pass and mixed pass/fail scenario sets
  - first-failure index semantics (`SIZE_MAX` for all-pass, concrete index otherwise)
  - tiny compat buffer/api-version behavior checks
- Added/updated unit coverage for:
  - policy override round-trips and invalid data handling
  - effective connect-info policy application
  - runtime capabilities contract

## In Progress

- Keep parity guardrails tight while layering media-plane embed contract
  in parallel with control-plane workstream.

## Next (Checklist Status)

1. Wire core-managed auto-recover config + tuned runtime recover-result + status semantics into deckstation FFI boundary contract.  
   Status: Mostly Done (core + FFI boundary landed; final harness-default path still being tightened).
2. Add host-side smoke harness usage of runtime capabilities + auto-recover loops.  
   Status: Done (consolidated with #3 via baseline detailed harness integration).
3. Expand E2 diagnostics/events + recovery actions into host-side smoke harness mapping.  
   Status: Done (consolidated milestone complete; detailed baseline output + deterministic gate summary consumed in host harness path).
4. Begin E3 audio/sync planning with parity-safe gating.  
   Status: Verification-Ready (gates and deterministic validation checklist are in place; code paths remain gate-off by default).

### E3 Planning Notes (Parity-Safe)

- Keep E3 pathways disabled by default behind explicit environment gates.
- Preserve existing runtime fallback behavior regardless of E3 gate state.
- Require harness parity checks before/after enabling any E3 subgate.
- Stage activation order:
  1) master E3 gate scaffold
  2) audio-path subgate
  3) sync-path subgate
  4) gated validation captures

E3 done criteria (verification-ready):
- [x] E3 gate model is explicitly documented (master + audio + sync).
- [x] deterministic OFF/ON validation checklist is documented.
- [x] script-backed verification references are documented:
  - `/Users/alvyn/Projects/Flutter/deck_station/scripts/e2e3-verify-matrix.sh`
  - `/Users/alvyn/Projects/Flutter/deck_station/scripts/phased-run-case.sh`
  - `/Users/alvyn/Projects/Flutter/deck_station/scripts/control-verify-case.sh`
- [x] parity-preserving fallback expectations are explicit across gate states.

### E2/E3 Deterministic Gating Verification Checklist

Script-backed verification references:
- `/Users/alvyn/Projects/Flutter/deck_station/scripts/e2e3-verify-matrix.sh`
- `/Users/alvyn/Projects/Flutter/deck_station/scripts/phased-run-case.sh`
- `/Users/alvyn/Projects/Flutter/deck_station/scripts/control-verify-case.sh`

JSON + Markdown verification workflow:
1. JSON-style core summary contract verification:
   - `flutter test test/chiaki_headless_ffi_test.dart` (workdir: `/Users/alvyn/Projects/Flutter/deck_station`)
   - expected: tests pass and `compactSummary` assertions pass in FFI map contract.
2. Deterministic E2/E3 matrix verification:
   - `bash /Users/alvyn/Projects/Flutter/deck_station/scripts/e2e3-verify-matrix.sh --dry-run`
   - expected: per-case expected checks and final `PASS cases`/`FAIL cases` summary.
3. Markdown benchmark output generation:
   - `/Users/alvyn/Projects/Flutter/deck_station/scripts/phaseb-summarize.sh <log_file> <mode> <spec_override> "<key settings>" "<playability>"`
   - expected: `Markdown row:` and one pipe-delimited benchmark row.
4. Markdown benchmark append:
   - `/Users/alvyn/Projects/Flutter/deck_station/scripts/phaseb-record.sh /Users/alvyn/Projects/Flutter/deck_station/PSCLOUD_PORT_TRACKING.md <log_file> <mode> <spec_override> "<key settings>" "<playability>"`
   - expected: row appended once (or duplicate-skip message) without unrelated file mutations.

Fast preflight (no stream launch):
- `bash /Users/alvyn/Projects/Flutter/deck_station/scripts/e2e3-verify-matrix.sh --dry-run`

1. E2 gate OFF (core media start blocked):
   - Command:
     - `CHIAKI_MEDIA_E2_ENABLE=0 cmake --build /Users/alvyn/Projects/Python/chiaki-ng/build-current --target chiaki-unit -j 8`
     - `CHIAKI_MEDIA_E2_ENABLE=0 ctest --output-on-failure -R unit` (workdir: `/Users/alvyn/Projects/Python/chiaki-ng/build-current`)
   - Expected:
     - tests pass
     - gated start remains parity-safe (`CHIAKI_ERR_UNINITIALIZED`)

2. E2 gate ON (core media start enabled):
   - Command:
     - `CHIAKI_MEDIA_E2_ENABLE=1 cmake --build /Users/alvyn/Projects/Python/chiaki-ng/build-current --target chiaki-unit -j 8`
     - `CHIAKI_MEDIA_E2_ENABLE=1 ctest --output-on-failure -R unit` (workdir: `/Users/alvyn/Projects/Python/chiaki-ng/build-current`)
   - Expected:
     - tests pass
     - real lifecycle path (create/start/stop/join/destroy) stays valid

3. E3 gates OFF (host parity baseline unchanged):
   - Command:
     - `flutter analyze lib/main.dart` (workdir: `/Users/alvyn/Projects/Flutter/deck_station`)
   - Expected:
     - analyze passes
     - host smoke summary format remains deterministic baseline

4. E3 master ON, subgates OFF:
   - Command:
     - `DECKSTATION_MEDIA_E3_ENABLE=1 flutter analyze lib/main.dart` (workdir: `/Users/alvyn/Projects/Flutter/deck_station`)
   - Expected:
     - analyze passes
     - no functional behavior change from E3-off path

5. E3 audio/sync subgates ON:
   - Command:
     - `DECKSTATION_MEDIA_E3_ENABLE=1 DECKSTATION_MEDIA_E3_AUDIO_ENABLE=1 DECKSTATION_MEDIA_E3_SYNC_ENABLE=1 flutter analyze lib/main.dart` (workdir: `/Users/alvyn/Projects/Flutter/deck_station`)
   - Expected:
     - analyze passes
     - gated activation only; fallback safety and parity guardrails preserved

### Consolidated Milestone (#2 + #3): Host Harness Integration

Done criteria:
- [x] Core exposes deterministic parity baseline execution details and aggregate parity/smoke outputs via compat contracts.
- [x] DeckStation harness path consumes detailed baseline output when capability is available.
- [x] Harness keeps backward-compatible fallback path for older core builds.
- [x] Host-visible gate summary is deterministic (`PASS|FAIL`, smoke pass/total, fixture pass/total, first failing scenario).

Verification commands:
- `cmake --build /Users/alvyn/Projects/Python/chiaki-ng/build-current --target chiaki-unit -j 8`
- `ctest --output-on-failure -R unit` (workdir: `/Users/alvyn/Projects/Python/chiaki-ng/build-current`)
- `flutter analyze lib/main.dart` (workdir: `/Users/alvyn/Projects/Flutter/deck_station`)

## Verification Snapshot

- Build target: `chiaki-unit` (pass)
- Test command: `ctest --output-on-failure -R unit` (pass)
