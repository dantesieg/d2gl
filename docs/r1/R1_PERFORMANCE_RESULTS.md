# R1 performance results

## Measured baseline (before R1; Steam Deck OLED, Proton, HDR on, 1280x800, frame_latency=1, gpu_sync=0, ~20k frames)
| ms (mean)   | fast frames (n=19940, ~90 fps) | slow frames, big fights (n=567, ~45 fps) |
|-------------|------|------|
| game_frame  | 11.1 | -    |
| game_work   | 2.6  | -    |
| game_wait   | 7.4  | -    |
| game_cpu    | 3.2  | 19.0 |
| render_wait | 0.0  | 17.5 |
| render_busy | 0.5  | -    |
| fence       | 3.8  | 2.7  |
| swap        | 5.8  | 0.6  |
| limiter     | 0.8  | -    |
| render_cpu  | 1.3  | -    |
| gpu         | 2.4  | 0.9  |

Conclusion: slow frames are bound by Diablo II's game-thread CPU (19 ms), not GPU or render sync.
R1 cannot fix that. Fast frames spend most time waiting on fence + vsync/present.

## R1 results
None measured. `early_slot_release=true` has not been run on hardware.
Expected (theoretical): fast-frame `game_wait_ms` down by up to the fence time (~3.8 ms); interval/fps at
vsync unchanged; slow frames unchanged.

## How to measure
`perf_log=true`, same scene, A/B `early_slot_release=false/true`; compare game_wait, interval p99, fence, swap.
