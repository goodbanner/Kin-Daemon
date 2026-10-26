# Kin Runtime Status Protocol

`AI/auto_status.txt` and `AI/capabilities.txt` are daemon-owned, read-only snapshots for the App.

## File rules

- Files use UTF-8 key/value lines: `key=value`.
- The daemon writes through a same-directory temporary file, completes the write, calls `fsync`, then renames it over the target.
- Readers must tolerate missing files, unknown keys, empty values, and older schema versions.
- A missing capability is reported as unavailable; it must not prevent unrelated scheduling.
- While `mode=auto`, `auto_status.txt` is sampled at approximately 1 Hz and must not be interpreted as a hardware write acknowledgement.
- When the daemon leaves auto it writes exactly one invalidate snapshot, then stops updating the file until auto is entered again. That frame sets `resolved` to the new non-auto mode (for example `balance`), `fsm=INACTIVE`, `cpu_max_little=-1`, `cpu_max_big=-1`, and refreshes `batt`, `cur_ma`, `temp`, and `timestamp`. The file is not deleted. Further non-auto ticks must not publish another snapshot or refresh FSM caps. `alpha=-1` on that frame means there is no live FSM decision.

## auto_status keys

Existing keys retain their historical meaning. The App parser accepts the following optional metadata keys; daemon producers should add them only when the corresponding decision value is available:

| Key | Unit | Meaning |
| --- | --- | --- |
| `schema_version` | integer | Status schema version; absent means version 1 |
| `seq` | integer | Monotonic snapshot sequence when present |
| `timestamp` | Unix seconds | Snapshot creation time when present |
| `resolved` | string | Resolved mode (`fsm` or a profile) |
| `fsm` | string | Current FSM state |
| `resolved_policy` | token | Final ordinary CPU cap source, or `rom_managed` in the Fengchi branch |
| `policy_reason` | token | Binding cap reason (e.g. `frame_target`, `category`, `joint_dvfs`), candidate reason, or `fengchi_takeover` |
| `policy_scope` | token | `cpu`; these diagnostics do not describe the independent GPU policy |
| `fas_state` | token | `engaged`, `inactive` (including warm-up/unavailable), or `blocked` by Fengchi takeover |
| `write_verification` | token | `daemon_write_result`: describes the daemon writer result; it is not a hardware acceptance/readback assertion |
| `last_write_nodes` | string | Per-node tally of the last FSM CPU write batch (`total=,written=,unchanged=,failed=`, plus `unknown=`/`dropped=` when non-zero). Absent when the batch recorded no node path, or when Fengchi takeover skipped the batch. Counts daemon writer results, not kernel acceptance. |
| `last_write_failed` | string | Comma-separated node paths that failed in that batch, at most 4, sanitized to path-safe characters. Absent when nothing failed. |
| `battery_state` | token | `disabled`, `charging`, `normal`, `low`, `critical`, `unknown`, or `low_held_unknown`; describes the actual low-battery hysteresis used for alpha |
| `foreground_package` / `foreground_category` | string | Daemon's last verified foreground app and category; absent means unavailable |
| `bucket` | integer | Smart bucket 0..11 |
| `daypart` | integer | Smart day-part index |
| `alpha` / `alpha_raw` | x1000 | Smart power-saving tendency and raw bucket value |
| `conf` | x1000 | Smart confidence |
| `cpu_max_little` / `cpu_max_big` | MHz | Current target CPU caps. The daemon writes MHz (`kHz/1000`). `-1` means no active FSM cap (the leave-auto invalidate frame). |
| `temp` | Celsius | Decision temperature |
| `headroom` | percent | Vendor thermal headroom; valid only when `headroom_valid=1` |
| `gpu` / `load1` / `cpu_busy` / `cur_ma` | mixed | Runtime telemetry |

The additional policy keys are schema-2 diagnostics; legacy `resolved=fsm` and `reason=smart_policy` remain unchanged. For registered caps that tie at a final cluster maximum, the summary reason prefers thermal, battery, power, app category, camera, then FAS. A reason can describe a binding cap on either cluster; it is not a full per-cluster explanation or an acknowledgement from the hardware. `last_write_scope` identifies the daemon writer path (for example `fsm_cpu_batch`) and `last_write_result` is `written`, `unchanged`, `failed`, or `unknown`. `last_write_nodes`/`last_write_failed` narrow that single result to the individual nodes the daemon attempted, so a batch that was entirely `unchanged` can be told apart from one that partially failed; they still do not prove the kernel kept the value, and nodes written outside `FileWriteIfChangedBlocking` (trigger writes, GPU atomic channel, uclamp boost) are not counted. Battery state is sourced from the alpha controller's hysteresis; the final resolver also applies an explicit battery cap after FSM overrides/boost.

The App must show `stale` when `timestamp` is older than its freshness threshold and must not label stale data as a successful live write.

## capabilities keys

Capability keys use `<name>_exists=0|1`, with optional `<name>_reason` and resource-specific values such as `_max_khz` and `_min_khz`. The daemon probes nodes read-only. `node_missing`, `permission_denied`, and `read_failed` are informational reasons only.

## Ownership boundary

Kin-Daemon owns policy decisions and the existing resource writers. The App reads these snapshots and submits configuration through its existing transaction path; it does not write hardware nodes directly. A status snapshot describes a decision target, not proof that another kernel/vendor component accepted it.
