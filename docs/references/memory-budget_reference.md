# Memory Budget — twang target (EK-RA8D2)

Living record of flash/SRAM utilization for the two Zephyr target images
(`target/zephyr/cm33` and `target/zephyr/cm85`). Append one history row per
target build; the last row is current.

## Budget

The split is fixed by the RA8D2 memory map (`r7ka8d2kflcac.dtsi` in the Zephyr
tree) and does not change across builds.

| Core | Flash (code MRAM) | SRAM |
|---|---|---|
| cm33 (control) | 256 KB | 640 KB |
| cm85 (audio) | 768 KB | 1 MB |

The control core (cm33) is the constrained one — the entire panel UI ships
against a 256 KB flash ceiling, while the audio core has 768 KB.

## Pre-spike baseline (LVGL)

Historical reference, not part of the tracked series. Before the `spike`
renderer, the cm33 image used LVGL; its ~150 KB of fixed overhead (multi-format
blending, gradients, shadows, arcs, the default theme) put flash at **78.55% of
256 KB (~201 KB)** for a controller at only ~15% of its intended scope. The
LVGL→spike migration (2026-09-10) cut it to ~24% (~63 KB). This is why 256 KB
is treated as the hard ceiling: LVGL was consuming most of it before the real
UI existed.

## History

Percentages are of the budget above. Reproduce a build per AGENTS.md
"Target builds"; the linker prints the size table at the end of each build.

| Date | Commit | cm33 flash | cm33 RAM | cm85 flash | cm85 RAM | cm85 DTCM |
|---|---|---|---|---|---|---|
| 2026-09-17 | 81da9f4 | 71,852 B (27.4%) | 50,044 B (7.6%) | 48,284 B (6.1%) | 9,296 B (0.9%) | 3,516 B (5.4%) |
| 2026-09-18 | scope-tap | 72,428 B (27.6%) | 50,044 B (7.6%) | 49,208 B (6.3%) | 9,296 B (0.9%) | 3,516 B (5.4%) |
| 2026-09-18 | usb-composite | 72,428 B (27.6%) | 50,044 B (7.6%) | 94,016 B (12.0%) | 20,704 B (2.0%) | 3,516 B (5.4%) |
| 2026-09-18 | usb-audio-midi | 73,176 B (27.9%) | 50,044 B (7.6%) | 95,084 B (12.1%) | 22,952 B (2.2%) | 3,516 B (5.4%) |
| 2026-09-18 | rtt-counters | 76,668 B (29.3%) | 51,260 B (7.8%) | 96,344 B (12.3%) | 24,008 B (2.3%) | 3,516 B (5.4%) |
| 2026-09-18 | send-chain | 76,668 B (29.3%) | 51,260 B (7.8%) | 96,272 B (12.2%) | 25,256 B (2.4%) | 3,516 B (5.4%) |
| 2026-09-18 | control-thread | 77,072 B (29.4%) | 55,700 B (8.5%) | 96,272 B (12.2%) | 25,256 B (2.4%) | 3,516 B (5.4%) |
| 2026-09-18 | two-rings | 77,156 B (29.4%) | 55,708 B (8.5%) | 96,360 B (12.3%) | 25,256 B (2.4%) | 3,516 B (5.4%) |
| 2026-09-18 | panic-path | 77,292 B (29.5%) | 55,708 B (8.5%) | 96,360 B (12.3%) | 25,256 B (2.4%) | 3,516 B (5.4%) |
| 2026-09-18 | decouple-shared | 77,364 B (29.5%) | 55,708 B (8.5%) | 96,360 B (12.3%) | 25,256 B (2.4%) | 3,516 B (5.4%) |
| 2026-09-19 | allocator-note | 77,364 B (29.5%) | 55,708 B (8.5%) | 96,360 B (12.3%) | 25,256 B (2.4%) | 3,516 B (5.4%) |
