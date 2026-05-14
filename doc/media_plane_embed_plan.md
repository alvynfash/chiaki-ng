# Chiaki Media Plane Embed Plan (Flutter)

Last updated: 2026-05-14
Status: E2 in progress + Phase A external-frame ABI slice started

## Current Phase Snapshot (2026-05-14, appended)

Status:
- External-frame ABI slice (A1) is in place for DRM_PRIME/dmabuf metadata + fd handoff.
- Activation is not claimed complete yet; DeckStation-side native import/render validation is still the gating work.

Decisions locked:
- Keep existing CPU-plane callback path as compatibility fallback while A1/A2 validates.
- Treat native-only video path as the Phase A target behavior; Dart frame bridge remains fallback/debug only.
- Do not migrate ownership/recovery to host; Chiaki remains stream/recovery authority.

## Phase A Zero-Copy Activation Matrix (next)

| Matrix ID | Slice | Scope | Expected Evidence |
| --- | --- | --- | --- |
| `A1-C1` | ABI | external frame callback contract integrity (DRM_PRIME + dmabuf planes/fds) | struct/offset compatibility checks + stable callback invocation |
| `A1-C2` | Runtime | fd lifecycle correctness under sustained callback load | no fd leak/regression across run window; callback return-path stability |
| `A2-L1` | Linux import | DeckStation native dmabuf import + texture continuity | sustained frame continuity markers with native path active, no Dart frame hot path |
| `A2-F1` | Fallback | compatibility CPU-plane fallback remains callable | explicit fallback mode succeeds without lifecycle break |

## Zero-Copy Activation Acceptance Criteria (do not overstate)

Zero-copy activation is considered ready only when:
- A1 ABI/runtime checks (`A1-C1`, `A1-C2`) pass with recorded evidence.
- Linux native dmabuf import path (`A2-L1`) passes sustained continuity validation.
- Fallback compatibility (`A2-F1`) remains intact but is explicitly gated (not implicit hot-path fallback).
- Documentation in this file and DeckStation tracker is updated with timestamps, toggles used, and result summary.

## Goal

Expose a stable embedded media-plane API from `chiaki-ng` so Flutter can render
video/audio directly (texture + audio sink path) without replacing current
process-launch flow until the embedded path is proven.

## Non-Goals (initial)

- No transport algorithm changes.
- No parity baseline behavior changes.
- No forced migration from process-launch mode.

## Proposed Native Surface (v0)

Control:
- `chiaki_media_capabilities()`
- `chiaki_media_texture_create()` (platform-specific backend handle)
- `chiaki_media_start_cloud_strings(...)`
- `chiaki_media_stop()`

Event stream:
- state changes (`starting`, `running`, `stopped`, `error`)
- stream metrics (fps, dropped, decode errors, jitter)
- decode warnings (`no ref frame`, `rps error`) as structured counters

Frame/audio callbacks:
- video frame callback (pixel format + dimensions + timestamp)
- audio frame callback (sample format + channels + sample rate + timestamp)

## Threading and Lifetime

- Keep transport/session ownership native-side.
- Use dedicated decode thread(s) with lock-free queue to render boundary.
- Flutter side consumes only already-assembled media events/frames.

## Safety Gates

- Feature flag required for media-plane start.
- Existing process-launch path remains default and unchanged.
- Hard fallback to process-launch if media init/start fails.

## Phase Plan

Phase E1 (contract):
- define ABI and event schema
- add no-op capability endpoint
 - current status: `chiaki_media_capabilities` endpoint landed with unit coverage

Phase E2 (video bootstrap):
- texture creation
- first-frame path (video only)

Phase E3 (audio + sync):
- audio callback path
- basic A/V sync and buffering policy
- parity-safe gate visibility for host diagnostics:
  - `DECKSTATION_MEDIA_E3_ENABLE`
  - `DECKSTATION_MEDIA_E3_AUDIO_ENABLE`
  - `DECKSTATION_MEDIA_E3_SYNC_ENABLE`
  - surfaced through `ChiakiHeadlessRuntimeRecoveryCoreDiagnosticsSummary`
    (`e3_*_gate_enabled` + `e3_*_effective`) with default-off behavior preserved
  - surfaced through `ChiakiMediaSessionStats` as gate/effective telemetry plus
    hook counters (`e3_audio_hook_*`, `e3_sync_observation_count`,
    `e3_last_av_delta_us`) with no behavior change while gates are off

Phase E4 (input/haptics closure):
- ensure embedded path parity with existing input loop

## Done Criteria (for kickoff)

- API contract reviewed and frozen for E1.
- One runtime dry run can query capabilities and return structured response.

## Current E1 Progress

- Added core endpoint:
  - `chiaki_media_capabilities_size()`
  - `chiaki_media_capabilities_init(...)`
  - `chiaki_media_capabilities(...)`
- Added ABI-safe compat endpoint:
  - `chiaki_media_capabilities_compat(...)`
- Added explicit contract version markers:
  - `media_capabilities_version`
  - `media_event_schema_version`
- Current behavior is intentionally parity-safe and no-op:
  - reports capability-query support only
  - does not change runtime/session/media behavior yet

## Current E2 Scaffold Progress

- 2026-05-14 (Phase A / zero-copy slice A1):
  - Added headless external video frame ABI for dmabuf/DRM_PRIME transport:
    - `ChiakiHeadlessExternalVideoFrameType`
    - `ChiakiHeadlessDmabufPlane`
    - `ChiakiHeadlessExternalVideoFrame`
    - `ChiakiHeadlessExternalVideoFrameCallback`
    - `ChiakiHeadlessCallbacks.external_video_frame_cb`
  - Added `CHIAKI_HEADLESS_VIDEO_FORMAT_DRM_PRIME` mapping in headless format bridge.
  - Headless ffmpeg callback path now emits external frame metadata/fds for DRM_PRIME
    frames (fd dup/close ownership in callback window) while preserving existing
    CPU plane callback path as compatibility fallback.

- Added typed media session bootstrap APIs:
  - `chiaki_media_session_create(...)`
  - `chiaki_media_session_destroy(...)`
  - `chiaki_media_session_start_cloud_strings(...)`
  - `chiaki_media_session_stop(...)`
  - `chiaki_media_session_get_stats(...)`
  - `chiaki_media_session_stats_size(...)`
  - `chiaki_media_session_stats_init(...)`
  - `chiaki_media_session_get_stats_compat(...)`
  - `chiaki_media_session_wait_for_video_frame(...)`
  - `chiaki_media_session_wait_for_event(...)`
  - `chiaki_media_session_wait_for_readiness(...)`
  - `chiaki_media_readiness_report_size(...)`
  - `chiaki_media_readiness_report_init(...)`
  - `chiaki_media_session_wait_for_readiness_compat(...)`
  - `chiaki_media_health_report_size(...)`
  - `chiaki_media_health_report_init(...)`
  - `chiaki_media_session_get_health_report(...)`
  - `chiaki_media_session_get_health_report_compat(...)`
  - `chiaki_media_session_get_health_report_with_policy(...)`
  - `chiaki_media_session_get_health_report_with_policy_compat(...)`
  - `chiaki_media_session_get_health_report_with_profile(...)`
  - `chiaki_media_session_get_health_report_with_profile_compat(...)`
  - `chiaki_media_health_policy_init(...)`
  - `chiaki_media_health_policy_size(...)`
  - `chiaki_media_health_policy_defaults(...)`
  - `chiaki_media_health_policy_defaults_compat(...)`
  - `chiaki_media_health_policy_from_profile(...)`
  - `chiaki_media_health_policy_from_profile_compat(...)`
  - `chiaki_media_health_policy_profile_count(...)`
  - `chiaki_media_health_policy_profile_info(...)`
  - `chiaki_media_health_policy_profile_info_compat(...)`
  - `chiaki_media_health_policy_profile_from_key(...)`
  - `chiaki_media_health_policy_profile_from_key_compat(...)`
  - `chiaki_media_health_policy_profile_key_from_profile(...)`
  - `chiaki_media_health_policy_profile_key_normalize(...)`
  - `chiaki_media_health_policy_profile_key_normalize_compat(...)`
  - `chiaki_media_health_policy_profile_resolution_size(...)`
  - `chiaki_media_health_policy_profile_resolve(...)`
  - `chiaki_media_health_policy_profile_resolve_compat(...)`
  - `chiaki_media_session_diagnostics_snapshot_size(...)`
  - `chiaki_media_session_diagnostics_snapshot_init(...)`
  - `chiaki_media_session_get_diagnostics_snapshot(...)`
  - `chiaki_media_session_get_diagnostics_snapshot_compat(...)`
  - `chiaki_media_session_get_diagnostics_snapshot_with_policy(...)`
  - `chiaki_media_session_get_diagnostics_snapshot_with_policy_compat(...)`
  - `chiaki_media_session_get_diagnostics_snapshot_with_profile(...)`
  - `chiaki_media_session_get_diagnostics_snapshot_with_profile_compat(...)`
  - `chiaki_media_session_get_diagnostics_snapshot_with_profile_key(...)`
  - `chiaki_media_session_get_diagnostics_snapshot_with_profile_key_compat(...)`
  - `chiaki_media_health_evaluate_readiness(...)`
  - explicit lifecycle state machine in media-session stats (`created/starting/running/stopping/stopped/error`)
- Current behavior remains parity-safe:
  - create/destroy are local object lifecycle only
  - start path is gated by `CHIAKI_MEDIA_E2_ENABLE`
  - when gate is off, start returns `CHIAKI_ERR_UNINITIALIZED`
  - when gate is on, start uses real headless create/start path
  - stop is idempotent success for valid session handle
  - callback counts + last event type are exposed via media session stats API
  - first-frame readiness can be polled deterministically with timeout
  - ready-event + first-frame readiness can be polled deterministically with timeout
  - lifecycle state transitions are exposed directly to host orchestration
  - per-event diagnostics now include:
    - `ready_event_count`, `stopped_event_count`, `error_event_count`
    - `last_event_monotonic_us` for host-side stream-health timelines
  - host-side struct-size compatibility helpers are available for stats/readiness
    consumers so future field growth remains ABI-safe
  - host-side orchestration can query a single health verdict (`idle/starting/ready/degraded/terminal`)
    instead of rebuilding readiness heuristics per client
  - health verdict computation is policy-based (default policy in core), enabling host
    tuning for degraded vs terminal thresholds without ABI breaks
  - session-level health query now supports caller-supplied policy directly for
    per-platform/per-network tuning without host-side re-evaluation logic
  - health-policy defaults are discoverable through explicit API + compat contract,
    so host bindings can avoid hardcoding threshold values
  - hosts can request named policy profiles (`default/aggressive/conservative`) to
    switch behavior without manually tuning each threshold field
  - session health can now be queried directly by profile id, so host code does not
    need an intermediate policy-materialization step
  - profile catalog is discoverable from core (count + indexed profile info), so
    host UIs can render supported profile choices without hardcoded lists
  - profile info now includes stable metadata (`profile_key`, `display_label`) so
    host UI naming/order can follow core contract directly
  - stable key->profile lookup is available for persisted settings restore without
    host-side mapping tables
  - key normalization helpers allow case/format-insensitive input mapping to canonical
    profile keys before persistence
  - profile resolution API returns canonical key + enum + display metadata in one call,
    reducing host-side orchestration/branching
  - media capabilities now expose minimum struct sizes for stats/readiness/health/policy/profile
    payloads so host FFI can allocate buffers safely without hardcoded `sizeof` assumptions
  - consolidated diagnostics snapshot API returns stats + readiness + health in one call
    for consistent host polling and fewer FFI round-trips
  - session health can be queried directly by profile key string (`"default"`,
    `"aggressive"`, `"conservative"`), enabling key-based persisted preferences
  - media capabilities contract now advertises support for:
    - session lifecycle create/destroy
    - stats/readiness/health report APIs
    - compat-copy variants for stats/readiness/health
