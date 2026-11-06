# Frame timing analysis (static, from code; no measurements)

## Critical path per frame (legacy)
```
game:   [record N] -> capture -> release cpu[N] -> wait gpu[N+1] -> reset -> [record N+1]
render: wait cpu[N] -> upload VBO/PBO -> commands -> mod pass -> FENCE WAIT (GPU idle after)
        -> release gpu[N] -> Menu::draw -> SwapBuffers (vsync) -> limiter -> next
```
* With `frame_latency=1` (2 slots) the game can record N+1 while the render thread executes N.
  It blocks in `game_wait_ms` whenever the render thread (incl. GPU via the fence) is slower.
* The GPU never overlaps with the next frame's submission: render-thread time per frame is
  roughly `render_busy + fence (~GPU time not hidden) + swap + limiter`.
* Vsync blocks inside `SwapBuffers` (driver-dependent); the limiter is skipped when vsync is on
  (`setFpsLimit(!App.vsync && …)`).
* Pacing mode (`foreground_fps.pacing`) spins the render thread for the last ~1.5 ms of each
  period: expect `render_cpu_ms` close to the frame period in that mode.

## Expected signatures (hypotheses to test, not results)
| Symptom in CSV | Likely cause |
|---|---|
| high `fence_ms`, low `render_busy_ms` | GPU-bound (post-fx chain, upscaler) |
| high `render_busy_ms` | driver/CPU submission or large PBO uploads (area transitions, `m_tex_update`) |
| high `game_wait_ms`, `inflight` at max | render/GPU slower than the game; raising `frame_latency` hides jitter, adds latency |
| high `game_work_ms` | D2 simulation / module CPU work (monster density) |
| `interval_ms` spikes with normal busy/fence | vsync misses, compositor, limiter wake-up jitter |
| `dropped_samples > 0` | writer thread starved (disk/IO); not a game problem |

## How to read stalls
Summaries flag frames above 2x the window median (`stalls_2x_median`). Join the CSV on `frame`
to see which component grew. `gpu_ms` lags by >= 1 frame; use `gpu_frame` to align.
