# Bounded rendering performance acceptance

This protocol implements [ticket #11](https://github.com/dsun9/Yoink/issues/11) under [the rendering specification, issue #1](https://github.com/dsun9/Yoink/issues/1). It measures a visible sprite through the production native hook, `pp_platform_poll`, runtime event/tick, interaction and easing, the app-lived shared source worker, retained OpenGL drawing, returned native swap, and explicit GPU completion. The same probe, receiver and desktop supervisor provide the measurements. Hosted CI checks deterministic behavior; machine timing stays in this opt-in desktop command.

## Repeatable commands

Build all Release targets, then run correctness and performance separately from the repository root:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --parallel 4 -- /nodeReuse:false
ctest --preset windows-release -L headless --output-on-failure
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Configuration Release -BuildDirectory build/windows-release -TestRegex '^(shaped_editor|lifecycle|ui_density|ui_preview|bitmap_retained|scalable_quality|png_retained|direct_gl|sprite_flow|drawer_hidden|surface_failure|settings_smoke|windows_gif|windows_surface|windows_capture|windows_anchors)$' -LogPrefix rendering-correctness
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tests/platform/run_acceptance_desktop.ps1 -Performance -Configuration Release -BuildDirectory build/windows-release -LogPrefix rendering-performance
```

Both supervised desktop commands must pass. Run them exclusively with no competing build or test. Each supervisor has a 180-second hard timeout. The performance probe stops scheduling after 145 seconds, leaving time for cleanup. `-BuildDirectory` accepts an isolated checkout's absolute build directory. A Debug run cannot satisfy acceptance.

The supervisor preserves child failures, refuses held input, holds the desktop mutex, and restores injected input, the exact saved cursor and foreground window. Failed recovery fails acceptance. A suspended receiver child enters a private kill-on-close Windows job before it runs, so even an abrupt probe exit cannot orphan its receiver. The normal path closes the receiver gracefully. Runtime consumers are destroyed before the app-lived source owner joins its worker, before CRT exit. No driver or global display setting changes are part of this recipe.

Logs are `build/rendering-correctness-desktop.log`, its stderr companion, and the equivalent `rendering-performance` files. Preserve failed and incomplete logs with distinct prefixes. Keep supervisor stdout containing saved/actual HWND and restoration results, plus CTest details and the post-run owned-process inventory.

## Required visible matrix

There are 96 cases: six sources, four logical longest sides of 96/384/768/1536 DIP, drawable densities 1.0/1.5, and starting rotations 0/17.25 degrees. The sources are installed default Segoe UI Emoji, explicit bundled bitmap Noto, `alpha.png`, `disposal.gif`, the fixed arrow SVG, and mixed `A`/hand/newline/`B` text. The log records actual font family, style, version, scalable/bitmap/color capabilities, snapshot revision and canonical item font size separately from sprite pose.

One visible runtime sprite sits above the independent native receiver. The probe reuses its native context through the ladder and replaces source snapshots through scene ownership. `SOURCE` records snapshot creation separately from `COLD` endpoint preparation. Each `COLD` record reports settled wall time, accumulated owner work, first-presentation owner work, private worker generation/builds, owner builds, context creation, uploads, CPU submission, GPU execution/completion wait, returned swap and native placement. Every cold operation must have equal draw, GPU-execution and completion counts, and each settled cold case must contain a draw. A latched mismatch cannot be compensated by a later operation. The executable gate requires all 96 cold cases and matching aggregate counts, reported in `COLD TOTAL`. These phases may overlap; their durations are not additive wall-time components. Recurring tagged input remains active through source creation, endpoint preparation and native placement. Initial app/runtime setup before the tagged helper starts is explicitly labeled; its input responsiveness is unobserved.

Every case records three explicit active warmups and twenty active measured samples. Each sample injects a real fractional wheel event through the native hook and requires a normalized event owned by that sprite, real easing, a transform draw and matching execution/completion observations for every draw. Idle ticks cannot substitute. Samples include all owner work before and during the accepted wheel and while its helper returns, including genuine preceding easing draws. Scheduler idle/wake time is recorded through input/sharp latency rather than counted as render work. The summed owner work and renderer draw totals must agree; multiple draws are retained in the complete sample.

Density simulation changes both desktop-units-per-DIP and drawable-pixels-per-DIP coherently for the selected physical monitor, while retaining its physical bounds. The report labels actual host density, simulation, requested pose, native bounds and realized SDL drawable pixels. It validates matching native/drawable dimensions and the physical input target inside that rectangle. On the measured 150% host, the required 150% cases are physical and 100% is simulated. This is not a physical mixed-display check.

Each case reports median, nearest-rank p95 and maximum for complete owner work, source acquisition, CPU draw submission, GPU execution, explicit GPU-completion wait, returned native swap, and native sizing/placement. Three warmups are reported individually and excluded from those twenty-sample distributions. The p95 is the nineteenth sorted value. Both largest default-Segoe rotation cases must have complete warm p95 at most 16.7 ms. Other sources report their own costs and do not create a universal 60 Hz guarantee.

Direct window contexts request swap interval zero in production. Runtime scheduling and native input delivery pace owner work. The probe records the actual interval and verified synchronous debug-error policy or query fallback once per actual context. An unsupported interval request retains the driver's policy and must still pass the same measured gates. No asynchronous submission result substitutes for GPU completion.

Static-source rotation must add zero source builds/uploads. All measured transform draws must have matching completed GPU observations. GIF uses real instance elapsed clocks; a due frame is a content change and may upload its identified retained source. Translation and unchanged-content reuse, fractional resizing/latest replacement, geometry, CPU/GPU/capture limits, worker reservations, cancellation and rollback also have deterministic presenter/runtime CI checks. The seed-only SVG-to-PNG regression holds the first endpoint, verifies rejection preserves output/pose/revision, and requires exact source-charge release after acceptance, including an independent old consumer.

## Input and sharp gates

The bounded movement helper sends tagged alternating one-pixel input every 10 ms. The maximum tagged timestamp-to-hook-arrival wait must be at most 50 ms, including preparation, completed-but-undispatched source work and native publication. `GetTickCount` supplies the existing millisecond arrival boundary; QPC records helper send, normalized event, handling and publication attribution. Callback duration remains separate. Slow observations identify the owner phase and the gap since its last operation. Untagged desktop input does not satisfy the observation requirement.

After the default ladder, twenty actual fractional Alt-wheel enlargements request distinct physical endpoint targets near the nominal largest size. Each sample starts with the measured consumer settled and no pending shared job, requires exactly one new worker request/build/completion, and requires tagged input while that worker is pending. With only this visible consumer and hidden anchors, the pending hook counter is correlated to its request; a global pending counter alone would not identify an arbitrary consumer.

The completed sharp result must match the latest accepted normalized input timestamp, sprite ID, immutable snapshot revision, logical endpoint and density exactly. Its retained publication must succeed and all intervening draws must have GPU execution/completion observations. Candidate presence, worker readiness and a publication counter alone do not satisfy this check. The sole completion dispatcher is `pp_content_sources_poll`; the probe never drains the raw worker or destroys a service-owned worker.

Latest normalized resize input to matching completed publication must be at most 200 ms. No artificial delay is inserted. The log reports each sample's helper-to-hook, event-to-handling, private generation, observed-ready-to-publication upper bound, owner-operation maximum and all draw/completion counts, plus median/p95/max for sharp latency and generation. The ready timestamp is the owner's observation of completed but undelivered work, not the worker's exact completion instant.

Any breach, missing source/font, incomplete case/warmup/sample, unchanged or untagged sample, missing completion, missing pending overlap, mismatched endpoint/timestamp, protective stop or timeout fails. The 250-ms owner-operation and 100-ms tagged-arrival protective limits stop escalation; they do not weaken 16.7/50/200 acceptance gates. Process-only `performance_runner` controls check success, each budget, missing/idle/warmup/pending/target observations, output overflow and timeout before desktop initialization. The cold controls also reject an empty sequence, a missing first GPU observation, a missing first completion, compensating operation counts and an omitted cold case through the same supervisor. `performance_receiver_ownership` checks kill-on-close with a CPU/process-only self-child.

Every probe translation unit writes diagnostics into a fixed 8 MiB memory buffer. No synchronous stdout write or flush runs while tagged input is active. Overflow latches a failure instead of flushing or discarding observations. The final write follows input-helper, receiver, runtime, renderer and worker cleanup, and records bytes, capacity, overflow and whether cleanup preceded the flush. The pure runner uses an actual anonymous pipe to verify deferred writes, exact final output and overflow without an automatic flush. This keeps diagnostic I/O outside the measured owner loop while retaining every observation.

## Quality, native behavior and evidence limits

The correctness command covers premultiplied alpha/color, rectangular targeting with transparent interiors/padding/corners, outside input, accepted presses, capture, consumed release, hidden/zero-opacity state, nonactivation, overlap with sprites/Settings/drawer/receiver, first clicks, stationary-pointer changes and failed interceptor cleanup. Settled scalable/mixed/SVG exports match fresh requested-resolution output at zero/fractional rotation with the existing independent premultiplied reference and unchanged two-byte quality gate. The original six quality fixtures retain their existing narrow boundary exception, limited to four pixels. Two additional fixtures use the installed default Segoe and production mixed-text defaults at 1536 DIP/150%, with zero/17.25-degree rotation. They require raw channel error at most two at every pixel, with no exception. Every fixture compares complete fresh RGBA/coverage arrays and checks geometry, alpha and nonempty output. Bitmap tests retain available strike/native image detail and do not claim invented vector detail.

The full matrix uses the direct retained GL backend. Compatibility costs must remain separate. Historical legacy readback/layered measurements below are failing evidence, not current compatibility acceptance; no current Cocoa desktop timing is inferred from them. A compatibility implementation on another machine must be measured under its actual selected backend and must state unmet budgets.

GPU completion plus returned swap establishes the measured GPU/native API boundary. It does not prove DWM-visible frame cadence, scanout or fullscreen behavior. Physical mixed-display/fullscreen and native macOS window/input/Retina/Spaces remain unverified on this Windows host. Hosted Windows/macOS configure/build/headless/package results are separate evidence.

## Preserved observations

The original 2026-10-11 legacy Noto 2.051 path failed after a 109-ms tagged-input stop, with 15/96 cases and 303/1920 samples. Largest fallback 1536/150/zero had median 80.741/p95 93.183/max 98.823 ms, source p95 69.207 ms, draw p95 .027 ms, readback p95 8.768 ms and native publication p95 14.747 ms. The required installed-Segoe envelope and sharp observations were unavailable.

Tickets 8-10 preserve additional incomplete warm/input failures and 22 unexplained historical white first-use compositor captures. Later unchanged-code correctness passes do not establish their cause or a remedy. Ticket 11's initial visible collector also failed before normalized wheel input due to a measured DPI mismatch. The physical request 1920,1080 became cursor 2880,1620 outside the 96x90 sprite. The corrected private process setup restores exact DPI scope and desktop state. An earlier thread-only DPI restoration failure remains separate from the later verified process/thread setup.

The interval-one visible baseline completed only five cases, 15 warmups and 100 active samples. Multi-draw owner p95 was approximately 66-100 ms, dominated by 16-ms native swaps; GPU execution was sub-millisecond. Interval zero reduced the first 96-DIP p95 to .879 ms, but that comparison still failed on a 172-ms tagged input at the next setup. A preceding run had 157 ms. These inter-operation stalls remain unresolved history; the later instrumented pass did not reproduce or explain them.

The later scoped default ladder passed 16 cases/48 warmups/320 active samples with all draw totals equal to completion observations. Required physical 150% p95 was 4.271 ms near zero and 1.245 ms near fractional rotation. Twenty exact pending-worker sharp publications had maximum 139.248 ms; all tagged input max 16 ms. This was scoped evidence, with `overall_96_case_acceptance=not-evaluated`.

The first full matrix failed after six cases/18 warmups/120 active samples, with tagged input at 141 ms. Its measured 158.845-ms statistics/stdout flush immediately preceded the delayed hook, with 154.821-ms send-to-hook QPC latency and a 9.058-ms current owner operation. This attributes that run's delay to probe reporting I/O. The fixed memory buffer removes that active synchronous I/O. It does not establish the cause of the older 157/172-ms observations. The next full matrix passed, and the final run below added separate cold-phase metadata without changing behavior or gates.

The first additional default-quality run failed because the rotated mixed fixture counted twelve numerical boundary pixels against the original four-pixel allowance, although every raw channel error was at most one. The final new fixtures use the stricter all-pixel raw-error policy described above. The original six policies remain unchanged. Both failure logs remain preserved.

## Final Windows acceptance, 2026-10-12

The final attributed Release run passed in 8.14 seconds through the direct retained GL backend, with actual swap interval zero and verified synchronous KHR debug callback. The GPU was NVIDIA GeForce RTX 5090, OpenGL 3.2.0 NVIDIA 610.62. The actual host density was 150%; 100% cases were coherent simulations with realized native/drawable dimensions checked. The installed scalable color font was Segoe UI Emoji Regular Version 1.70, with COLRv1 layers and no bitmap strikes. Explicit fallback Noto Color Emoji Version 2.051, pinned revision `e92753bfa55fd449e427d4d325f9c8c40408c74e`, had a 109-em bitmap strike and no scalable outlines. Mixed text's actual primary font was Aptos Bold Italic Version 2.01;O365. Canonical item font size was 96 DIP, independent of the measured sprite pose.

Both required installed-Segoe cases used 1536 DIP and physical 150% density. Complete owner times include every genuine draw and its returned swap/GPU completion.

| Starting rotation | Median ms | p95 ms | Maximum ms | Required p95 limit ms |
| --- | ---: | ---: | ---: | ---: |
| 0 degrees | 0.707 | 1.895 | 6.950 | 16.7 |
| 17.25 degrees | 0.721 | 0.831 | 2.216 | 16.7 |

All six sources completed their sixteen cases. Their worst complete-owner p95 values across the ladder were:

| Source | Worst p95 ms |
| --- | ---: |
| Installed default Segoe | 3.382 |
| Explicit bitmap Noto | 1.511 |
| PNG alpha fixture | 8.369 |
| GIF disposal fixture | 2.273 |
| SVG arrow | 1.981 |
| Mixed text | 10.711 |

All 96 cases, 288 explicit warmups and 1920 active samples completed. All 6699 warmup/active draws had matching execution, GPU-completion and publication counts, with zero mismatches. All 96 cold records also had matching draw/completion counts. The probe observed 2777 tagged hooks, 2228 normalized wheel events and 278 hooks while source work was pending. Tagged input maximum was 31 ms against the unchanged 50-ms limit, including cold setup and endpoint publication.

All twenty sharp samples had actual pending-worker overlap and exact latest timestamp/endpoint/content-revision/density matches. Their input-to-completed-publication median/p95/max was 115.065/123.543/135.693 ms against the 200-ms limit. Private generation was 104.555/114.534/120.879 ms; maximum owner-operation distribution was 8.873/13.367/14.173 ms. Every intervening sharp draw had matching GPU-completion observations.

For the required zero-rotation case, cold settle took 99.960 ms with 93.952 ms of private generation, one worker build, 12.073 ms of accumulated owner work, 4.707 ms for the first presentation, 3.673 ms of upload, .345 ms of GPU-completion wait and 1.802 ms of native sizing/placement. Its two draws both completed. The following fractional case reused its source, with zero generation/build/upload, 3.593-ms settle and one completed draw. Initial runtime setup before the tagged helper took 521.013 ms and has no measured input-responsiveness claim. Every source/case's full phases remain in its `SOURCE`/`COLD` records.

The final buffer held 515571 of 8388608 bytes, with zero overflow and `flush_after_cleanup=1`. Exact original DPI scope restoration passed with a valid saved scope, setter error zero and semantic equality. The supervisor restored saved foreground HWND 67688, cursor 1637,1311 and released input; the original window remained valid. The post-run owned helper/receiver/application/compiler process inventory was empty, and cleanup destroyed the owned GL contexts and joined the source worker before the final flush.

Native correctness passed all sixteen selected tests in 106.41 seconds. The focused expanded quality run then passed all eight fixtures at both rotations in 2.65 seconds; the two actual-default fixtures had raw maximum error one. Final all-target Release compilation and all 31 headless checks passed, with headless runtime 42.77 seconds. Their controls include actual deferred-output/overflow behavior, abrupt receiver ownership, seed replacement/rollback and the strengthened protocol gates.

The authoritative final logs use prefix `issue11-final-attributed-acceptance`; the sixteen-test run uses `issue11-final-correctness`, and expanded quality uses `issue11-default-quality-green`. The external handoff `final-11.md` records their preserved absolute paths, source commit, parsed per-case JSON/CSV, red/green evidence and desktop recovery. Acceptance here covers the measured Windows machine and the protocol boundaries above.

## Final review verification, 2026-10-12

The final review repairs preserve the existing renderer routes and budgets. Bitmap replacement now refreshes canonical source mapping independently of reusable artwork/texture identity. One private mapping setter serves native, UI and export shaders. One bounded-target preflight serves request admission and capture, and the two unused request forwarding helpers are removed.

The public scene/presenter regression reproduced the font-size replacement defect before production changes. At a 192-DIP item font size, the old replacement's nontransparent artwork was 245.196 by 200.807 pixels versus a fresh pull's 490.393 by 401.615 pixels at the same pose. After the repair, both 96-to-192 and reverse replacements match fresh artwork placement at zero and 17.25 degrees, preserve pose and retain the texture without an extra upload. The native bitmap test independently maps displayed pixels from the fresh public source metadata. All four replacement comparisons passed with maximum RGB error one, byte-identical original 136-by-128 artwork and zero unnecessary uploads.

Cold acceptance controls were wired before changing the old executable gate. All five invalid cold sequences returned PASS and exit zero under that old gate. After the gate repair, all five return FAIL and exit one through the process-only supervisor. The new gate validates every operation, requires nonempty complete cold cases, and latches mismatches even when aggregate counts compensate. Earlier fixture setup failures remain separate from these valid reproductions.

The final full Release command used the same matrix and unchanged 16.7/50/200-ms limits, with log prefix `finalfix-performance`. It passed in 7.683 seconds, with the same RTX 5090, NVIDIA 610.62, installed Segoe UI Emoji 1.70, Noto 2.051 useful strike and actual Aptos primary font as the previous run. Physical host density remained 150%; 100% remains coherent simulation.

| Required Segoe, 1536 DIP and physical 150% | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Zero rotation | 0.723 | 1.633 | 3.843 |
| 17.25 degrees | 0.681 | 1.584 | 4.379 |

All 96 source/size/density/rotation cases, 96 strict cold cases, 288 warmups and 1920 active samples completed. The cold totals were 120 draws, 120 GPU observations and 120 completions, with zero latched mismatches. All 6708 warmup/active draws had matching observations and publication counts. Twenty sharp publications had exact latest timestamp/endpoint/revision/density agreement and actual pending-worker overlap; their median/p95/maximum was 114.590/128.808/136.137 ms. Private generation was 103.986/118.347/126.258 ms. Tagged input maximum was 31 ms, with 2766 hooks, 2228 normalized wheels and 273 pending hooks. Worst all-source warm p95 was 4.769 ms.

The required zero-rotation cold sequence settled in 98.811 ms, including 94.132 ms of private generation, one worker build and two completed draws. Accumulated owner work was 10.008 ms, first presentation 4.421 ms, upload 2.421 ms, GPU completion wait 0.599 ms and native placement 1.677 ms. Fractional reuse settled in 3.561 ms with one draw and zero generation/build/upload. Initial runtime work before tagged input was 476.255 ms, with input responsiveness unobserved. The buffer held 515614/8388608 bytes, overflow zero, and flushed after cleanup.

All-target Release compilation passed. The 31 headless checks passed in 44.30 seconds, including the cold controls and bitmap geometry regression; five additional audited process/input checks passed in 1.70 seconds. The exact sixteen-test native correctness command passed in 109.86 seconds, including all eight quality fixtures at both rotations. Actual-default Segoe and mixed fixtures retained the every-pixel maximum-two rule with raw maximum error one. Shared UI context restoration, rejection rollback, original source detail, alpha, input/capture and cleanup checks passed. DESIGN.md and the native UI appearance are unchanged; the strict contract audit and design lint reported zero errors and warnings.

Both native commands ran once under separate exclusive grants. Each supervisor restored exact saved/actual foreground HWND 67688 and cursor 1637,1311, with input released and the original window valid. Performance restored its valid saved DPI scope 80000022 with setter return 22, error zero, current awareness two and semantic equality. Both post-run inventories contained zero owned receiver/helper/application/compiler processes. GL contexts and workers ended with their owning fixtures, and performance recorded cleanup before output flush.

Raw logs, lossless parsed cases/phases/samples and their SHA-256 are preserved externally in the final-review-fixes handoff. The earlier ticket-11 measurements and failure history above remain unchanged. These results retain the stated limits on physical mixed-display/fullscreen behavior, native macOS and compositor-visible cadence.
