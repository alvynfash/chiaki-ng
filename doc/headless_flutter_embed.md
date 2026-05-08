# Headless Core Flutter Embed Notes (Linux)

This repository now exposes an additive headless facade in `chiaki/headless.h` on top of existing `chiaki_session_*` internals.

## Status snapshot (completed vs in-progress)

- Completed and currently verification-backed:
  - headless/runtime contract surfaces and unit-tested core APIs in `chiaki-ng`.
- In progress (not claimed complete here):
  - end-to-end live media-plane stabilization in Flutter embed playback.

Completed-milestone verification commands:
- `bash /Users/alvyn/Projects/Python/chiaki-ng/scripts/pscloud-core-verify.sh`
- `bash /Users/alvyn/Projects/Python/chiaki-ng/scripts/pscloud-core-verify.sh --mode full`
- `bash /Users/alvyn/Projects/Flutter/deck_station/scripts/core-port-verify.sh --with-chiaki`

## v1 Integration Contract

Native plugin side (Flutter Linux C++ plugin) should:

1. Create `ChiakiHeadlessSession` with callbacks.
2. Register decoded-frame callback (`video_frame_cb`).
3. Convert incoming YUV frame planes to plugin-owned RGBA buffer.
4. Publish that buffer through a Flutter `FlPixelBufferTexture`.
5. Mark frame available each callback (`fl_texture_registrar_mark_texture_frame_available`).

Audio callback path:

- `audio_frame_cb` receives decoded PCM S16 and should feed PipeWire/Pulse/SDL sink on native side.

Event callback path:

- lifecycle: `CONNECTING`, `READY`, `STOPPING`, `STOPPED`
- runtime stats: `STREAM_STATS`
- errors/warnings: `ERROR`, `WARNING`

## Threading + Buffer Lifetime

- Headless callbacks are invoked from worker threads (never UI thread).
- Frame/audio buffers are valid only during callback invocation.
- Plugin must copy data into owned buffers before returning.

## Minimal Dart surface

- `initCore() -> textureId`
- `startSession(config)`
- `stopSession()`
- `sendInput(controllerState)`
- `events` stream for lifecycle/stats/errors

## Validated Checkpoint (macOS PS Cloud)

Validated on branch `pscloud-port-iter` with:

- headless facade files (`chiaki/headless.h`, `lib/src/headless.c`)
- related CMake + unit-test wiring
- deadlock guards in:
  - `gui/src/streamsession.cpp` (`InitAudio`/`InitMic` same-thread bypass)
  - `gui/src/qmlmainwindow.cpp` (safe same-thread swapchain destroy path)

Observed result:

- deadlock popup resolved
- Chiaki launches and reaches playable PS Cloud video

Guidance for further porting:

- keep this checkpoint as known-good baseline
- add future changes in small isolated slices and re-test stream launch after each slice

## Tracking Source

Canonical cross-repo tracking log:

- `/Users/alvyn/Projects/Flutter/deck_station/PSCLOUD_PORT_TRACKING.md`
- `doc/pscloud_port_prd.md` (local core-port PRD tracker in `chiaki-ng`)

Use that file as the source of truth for:

- attempted changes
- test outcomes (pass/fail)
- known-good commit checkpoints
- next actions

## Headless Probe Contract (Experimental)

For non-invasive Flutter startup validation, headless now exports:

- `chiaki_headless_api_version()` -> returns API level (`19`)
- `chiaki_headless_probe()` -> returns `CHIAKI_ERR_SUCCESS` when linkage is valid

This avoids null-pointer session probe calls and keeps runtime launch path unchanged.

## Cloud ConnectInfo Builder

New helper:

- `chiaki_headless_connect_info_init_cloud_direct(ChiakiConnectInfo *, ChiakiHeadlessCloudLaunchInfo *)`

Intent:

- keep cloud-direct `ChiakiConnectInfo` assembly in core (single source of truth)
- simplify future Flutter/macOS/Linux plugin-side wiring for true headless runtime

Additional preflight helper:

- `chiaki_headless_probe_cloud_launch(...)`

Purpose:

- validate real cloud launch inputs end-to-end without starting/connecting a stream session
- allow host apps to fail fast on malformed launch data before runtime switch

String-contract preflight helper:

- `chiaki_headless_probe_cloud_launch_strings(...)`

Intent:

- host passes `morning_b64` and `regist_key_hex` directly
- base64/hex decoding and strict validation happen in core

Session lifecycle shims (experimental):

- `chiaki_headless_probe_create_session_cloud_launch_strings(...)`
- `chiaki_headless_probe_start_stop_session_cloud_launch_strings(...)`

Purpose:

- progressively validate runtime boundaries:
  - preflight decode/shape
  - create/destroy lifecycle
  - start/stop/join lifecycle
- keep host app fallback launch path unchanged while probing each boundary safely

## Runtime Contract Notes

Runtime cloud APIs are intentionally conservative and parity-safe:

### Realtime Audio Sink Contract (new, opt-in)

- Added runtime-configurable native audio sink boundary:
  - `chiaki_headless_runtime_set_audio_sink_config(...)`
  - `chiaki_headless_runtime_get_audio_sink_config(...)`
- Contract is **opt-in** and **parity-safe**:
  - when disabled/unset, existing headless audio callback behavior is unchanged.
  - no host restart/recover ownership is introduced by this contract.
- Sink lifecycle:
  - start: invoked lazily on first decoded audio readiness/frame path.
  - submit: invoked per decoded PCM frame (`S16`, channels/rate from stream).
  - stop: invoked on headless session stop/destroy.
- Realtime diagnostics for host telemetry:
  - `chiaki_headless_runtime_get_audio_sink_diagnostics(...)`
  - fields include submitted/dropped/underrun/start/stop counters.
- Host may report output underruns explicitly:
  - `chiaki_headless_runtime_audio_sink_report_underrun(...)`
- Runtime capability negotiation now exposes:
  - `supports_runtime_audio_sink_config_set_get`
  - `supports_runtime_audio_sink_diagnostics`
  - `supports_runtime_audio_sink_diagnostics_compat`
  - `supports_runtime_audio_sink_underrun_report`

- `chiaki_headless_runtime_cloud_start(...)`
  - struct-based runtime start path (`ChiakiHeadlessCloudLaunchInfo`) for
    host integrations that already hold decoded handshake/regist bytes
- `chiaki_headless_runtime_cloud_start_strings(...)`
  - returns `CHIAKI_ERR_INVALID_DATA` on malformed `morning_b64` / `regist_key_hex`
  - returns `CHIAKI_ERR_MUTEX_LOCKED` when a runtime session is already active
- `chiaki_headless_runtime_set_callbacks(...)`
  - sets callbacks for subsequent runtime-managed starts
  - accepts `NULL` to clear callbacks
  - returns `CHIAKI_ERR_MUTEX_LOCKED` when a runtime session is already active
- `chiaki_headless_runtime_set_stream_profile_overrides(...)`
  - optional runtime-only profile overrides (resolution/fps/bitrate/codec)
  - applies to subsequent runtime starts
  - accepts `NULL` to clear overrides
  - returns `CHIAKI_ERR_MUTEX_LOCKED` when a runtime session is already active
- `chiaki_headless_runtime_set_launch_overrides(...)`
  - optional runtime launch overrides (`ps5`, `enable_dualsense`, `enable_keyboard`)
  - applies to subsequent runtime starts
  - accepts `NULL` to clear overrides
  - returns `CHIAKI_ERR_MUTEX_LOCKED` when a runtime session is already active
- `chiaki_headless_runtime_set_overrides(...)`
  - atomic bundle setter for launch + stream profile + policy overrides
  - applies to subsequent runtime starts
  - accepts `NULL` to clear all runtime overrides
  - returns `CHIAKI_ERR_MUTEX_LOCKED` when a runtime session is already active
  - returns `CHIAKI_ERR_INVALID_DATA` for invalid override values (for example
    unsupported fps/resolution, codec, or zero bitrate when bitrate override is enabled)
- `chiaki_headless_runtime_get_overrides(...)`
  - snapshot current runtime overrides bundle (without starting a session)
  - includes policy override section when present:
    - `video_profile_auto_downgrade`
    - `enable_idr_on_fec_failure`
    - `packet_loss_max` (validated in `[0.0, 1.0]`)
- `chiaki_headless_runtime_patch_overrides(...)`
  - partial bundle update helper; can preserve unspecified override sections
  - supports `clear_unspecified` mode for stricter replace semantics
  - returns `CHIAKI_ERR_INVALID_DATA` for invalid override values
- `chiaki_headless_runtime_get_capabilities(...)`
  - returns runtime feature flags for host-side negotiation:
    - struct start support
    - overrides bundle/patch support
    - policy overrides support
    - sanity report support
    - state snapshot + compat snapshot support
- `chiaki_headless_runtime_get_capabilities_compat(...)`
  - size-aware compatibility read for bindings with older/smaller structs
- `chiaki_headless_runtime_capabilities_size()` / `chiaki_headless_runtime_capabilities_init(...)`
  - helper APIs for ABI-safe allocation and initialization
- `chiaki_headless_runtime_get_effective_launch_info(...)`
  - returns effective launch configuration after defaults + current runtime overrides
  - does not start/connect a session
- `chiaki_headless_runtime_get_effective_stream_profile(...)`
  - returns the effective profile after defaults + current runtime overrides
  - does not start/connect a session
- `chiaki_headless_runtime_get_effective_connect_info(...)`
  - returns final resolved `ChiakiConnectInfo` without starting a session
  - useful for host-side validation/debug before runtime start
  - returns `CHIAKI_ERR_INVALID_DATA` when resolved launch/profile values are invalid
- `chiaki_headless_runtime_get_sanity_report(...)`
  - one-call runtime diagnostics snapshot:
    - effective launch info
    - effective stream profile
    - runtime active flag
    - override-present flag
    - validation result
  - allows host apps to inspect and gate runtime start decisions
- `chiaki_headless_runtime_get_state_snapshot(...)`
  - consolidated host-integration snapshot:
    - api version
    - current runtime overrides
    - runtime sanity report
  - minimizes multi-call host glue for status/debug UIs
- `chiaki_headless_runtime_state_snapshot_size()`
  - reports current runtime snapshot struct size for host ABI guards
- `chiaki_headless_runtime_state_snapshot_init(...)`
  - zero/init helper that seeds `api_version`
- `chiaki_headless_runtime_get_state_snapshot_compat(...)`
  - size-aware compat copy API for bindings using older/smaller snapshot layouts
- `chiaki_headless_runtime_cloud_stop()`
  - idempotent: returns `CHIAKI_ERR_SUCCESS` when no runtime session is active
- `chiaki_headless_runtime_request_idr()`
  - requests an IDR frame on the active runtime session
  - returns `CHIAKI_ERR_INVALID_DATA` when no runtime session is active
- `chiaki_headless_runtime_get_media_diagnostics_snapshot(...)`
  - returns consolidated media stats/readiness/health from the active runtime session
  - uses default media health policy profile
- `chiaki_headless_runtime_get_media_diagnostics_snapshot_with_profile_key(...)`
  - same snapshot, but policy profile is selected by canonical profile key
  - allows host-side health semantics switching (`default` / `aggressive` / `conservative`)
- `chiaki_headless_runtime_get_media_diagnostics_snapshot_compat(...)`
  and `..._with_profile_key_compat(...)`
  - ABI-safe size-aware snapshot reads for bindings with older/smaller layouts
- `chiaki_headless_runtime_get_recovery_decision(...)`
  - returns a canonical runtime recovery recommendation based on diagnostics health
  - default profile key behavior (`default`)
- `chiaki_headless_runtime_get_recovery_decision_with_profile_key(...)`
  - same recovery recommendation but using explicit health profile key selection
- `chiaki_headless_runtime_get_recovery_decision_compat(...)`
  and `..._with_profile_key_compat(...)`
  - ABI-safe size-aware recovery-decision reads for bindings with older/smaller layouts
  - intended to keep host-side degraded/terminal recovery policy consistent across clients
- `chiaki_headless_runtime_apply_recovery_decision(...)`
  - applies a recovery action generated by runtime decision APIs
  - `REQUEST_IDR` maps to `chiaki_headless_runtime_request_idr()`
  - `STOP_RUNTIME` maps to `chiaki_headless_runtime_cloud_stop()`
- `chiaki_headless_runtime_recover(...)`
  and `chiaki_headless_runtime_recover_with_profile_key(...)`
  - single-step helper to compute and apply recovery action in core
  - keeps host logic thin: fetch/apply loop can be collapsed to one call
- `chiaki_headless_runtime_apply_recovery_decision_compat(...)`
  - ABI-safe compat wrapper for applying recovery decisions from size-variant host structs
- `chiaki_headless_runtime_get_recover_result(...)`
  and `..._with_profile_key(...)`
  - one-call decision+apply helper that returns:
    - selected recovery decision
    - applied action
    - apply error code
  - intended for thin FFI loops that want one deterministic runtime recovery call
- `chiaki_headless_runtime_get_recover_result_compat(...)`
  and `..._with_profile_key_compat(...)`
  - ABI-safe size-aware wrappers for recovery result structs
- `chiaki_headless_runtime_recovery_tuning_init(...)`
  and `chiaki_headless_runtime_recovery_tuning_size()`
  - define ABI-safe defaults for stateful runtime recovery loops
  - defaults: degraded streak threshold 3, IDR cooldown 2s, stop on terminal true
- `chiaki_headless_runtime_recover_tuned(...)`
  and `..._with_profile_key_tuned(...)`
  - one-call decision/apply with core-owned degraded-streak + cooldown state
  - reduces host-side policy duplication under bursty packet loss
- `chiaki_headless_runtime_recover_with_profile_key_tuned_compat(...)`
  - ABI-safe tuned-recovery call for older/smaller host structs
- `chiaki_headless_runtime_get_recovery_status(...)`
  and `chiaki_headless_runtime_get_recovery_status_compat(...)`
  - expose core recovery loop state (degraded streak, last IDR request time, runtime-active flag)
  - enables host telemetry/debugging without duplicating runtime internals
- `chiaki_headless_runtime_reset_recovery_status(...)`
  - explicitly resets core recovery loop state
  - useful for deterministic test harnesses and host-side session boundary cleanup
- `chiaki_headless_runtime_recovery_config_init(...)`
  and `chiaki_headless_runtime_set_recovery_config(...)` / `...get...(...)`
  - define and persist core-managed default recovery profile+tuning
  - removes host need to carry recovery profile/tuning state for normal operation
- `chiaki_headless_runtime_set_recovery_config_compat(...)`
  and `chiaki_headless_runtime_get_recovery_config_compat(...)`
  - ABI-safe config set/get for size-variant host bindings
- `chiaki_headless_runtime_set_recovery_profile_key(...)`
  and `chiaki_headless_runtime_get_recovery_profile_key(...)`
  - key-oriented recovery profile controls (`default`/`aggressive`/`conservative`)
  - avoids enum-coupling across FFI boundaries
- `chiaki_headless_runtime_recover_auto(...)`
  and `chiaki_headless_runtime_recover_auto_compat(...)`
  - uses core-managed recovery config to run one-call tuned recovery
  - intended as the default FFI-facing recovery path
- `chiaki_headless_runtime_simulate_recovery(...)`
  and `..._with_profile_key(...)`
  - offline recovery simulation from diagnostics snapshot + recovery config/tuning
  - returns simulated decision/action + next recovery status without touching live runtime
- `chiaki_headless_runtime_simulate_recovery_compat(...)`
  - ABI-safe simulation entrypoint for host-side parity harnesses and integration tests
- `chiaki_headless_runtime_simulate_recovery_sequence(...)`
  - deterministic multi-step simulation over a timeline of snapshots
  - enables parity/behavior checks for streak/cooldown logic in one call
- `chiaki_headless_runtime_simulate_recovery_sequence_report(...)`
  - summarized sequence outcome (action counts, first-stop index, terminal-health flag, final status)
  - useful for host-side parity dashboards and CI-style regression checks
- `chiaki_headless_runtime_simulate_recovery_sequence_compat(...)`
  and `..._sequence_report_compat(...)`
  - ABI-safe sequence simulation/report reads for size-variant host bindings

### Host Smoke + Telemetry Snippets

Keep host integration loops minimal by composing existing contracts directly.

#### 1) Parity smoke runner (single call, fixture+smoke result)

```c
ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult runner = {0};
ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_smoke_runner(
    expected_markers,   // ChiakiHeadlessRuntimeRecoveryParityFixtureExpected[N]
    actual_reports,     // ChiakiHeadlessRuntimeRecoverySimulationReport[N]
    scenario_count,
    &runner);
if(err == CHIAKI_ERR_SUCCESS && runner.smoke_result.fail_count > 0) {
    size_t idx = runner.smoke_result.first_failure_index;
    // report failing scenario index to host logs/metrics
}
```

Compat variant:

```c
ChiakiHeadlessRuntimeRecoveryParitySmokeRunnerResult out = {0};
ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_smoke_runner_compat(
    expected_markers_buf,
    scenario_count,
    expected_stride,
    expected_size,
    actual_reports_buf,
    report_stride,
    report_size,
    &out,
    sizeof(out));
```

#### 2) Fixture/replay export (build expected+report from one replay)

```c
ChiakiHeadlessRuntimeRecoveryParityFixtureExpected expected = {0};
ChiakiHeadlessRuntimeRecoverySimulationReport report = {0};

ChiakiErrorCode err = chiaki_headless_runtime_recovery_parity_fixture_export(
    "aggressive-loss-window",
    step_outputs,       // ChiakiHeadlessRuntimeRecoverySimulationStepOutput[step_count]
    step_count,
    &final_status,      // ChiakiHeadlessRuntimeRecoveryStatus
    true,               // validate_first_none_step_index
    &expected,
    &report);
```

Typical flow: replay/simulate -> export fixture+report -> persist/export JSON from host.

#### 3) Host status telemetry payload

```c
ChiakiHeadlessRuntimeHostStatus hs = {0};
ChiakiErrorCode err = chiaki_headless_runtime_build_host_status(
    &snapshot,          // ChiakiMediaSessionDiagnosticsSnapshot
    &recovery_result,   // ChiakiHeadlessRuntimeRecoveryResult
    &recovery_status,   // ChiakiHeadlessRuntimeRecoveryStatus
    &hs);
if(err == CHIAKI_ERR_SUCCESS) {
    // hs.health_state, hs.recovery_action, hs.degraded_streak,
    // hs.event_count/error_event_count/ready_event_count
}
```

Compat variant:

```c
ChiakiHeadlessRuntimeHostStatus hs = {0};
ChiakiErrorCode err = chiaki_headless_runtime_build_host_status_compat(
    snapshot_buf, snapshot_size,
    recovery_result_buf, recovery_result_size,
    recovery_status_buf, recovery_status_size,
    &hs, sizeof(hs));
```

This keeps host-side control flow simple and avoids ambiguous "already running" handling.

### 2026-05-10 Input/Haptics Parity Validation Notes

- Headless session event mapping now forwards core input/haptics parity markers over the existing headless event callback path as warning events (no control-plane migration):
  - `CHIAKI_EVENT_RUMBLE`
  - `CHIAKI_EVENT_TRIGGER_EFFECTS`
  - `CHIAKI_EVENT_MOTION_RESET`
  - `CHIAKI_EVENT_HAPTIC_INTENSITY`
  - `CHIAKI_EVENT_TRIGGER_INTENSITY`
  - `CHIAKI_EVENT_PLAYER_INDEX`
- Payload format is lightweight string telemetry intended for Flutter debug display / parity validation.
- Existing lifecycle/error events remain unchanged.

### 2026-05-10 Parity Soak Checklist Instrumentation Notes

- `ChiakiMediaSessionStats` now aggregates parity-soak checklist counters for A/V/input/haptics using existing callback/event flow:
  - A/V counters remain aggregate-only (`video_frame_count`, `audio_frame_count`, `e3_audio_hook_frame_count`, `e3_sync_observation_count`).
  - Input/haptics warning telemetry is bucketed into low-cost counters (`input_*_event_count` + `input_haptics_event_count`) plus one last-seen monotonic timestamp.
- No hot-path behavioral changes: no per-frame debug logs and no new frame/audio buffer copying paths.
