# perf_log output schema

## d2gl_perf.csv
Header (exact, from `perf::csvHeader()`):
```
frame,slot,inflight,t_ms,game_frame_ms,game_work_ms,game_wait_ms,game_cpu_ms,render_wait_ms,render_busy_ms,fence_ms,swap_ms,limiter_ms,interval_ms,render_cpu_ms,gpu_ms,gpu_frame
```
Units ms; integers for `frame`, `slot`, `inflight`, `gpu_frame`; `-1` = not available. Column
meanings: PERFORMANCE_BASELINE.md. One row per frame where `frame % perf_sample_every == 0`.

Format fixture (synthetic, produced by the unit test, **not a measurement**):
```
42,1,2,1000.500,16.6667,-1.0000,-1.0000,-1.0000,-1.0000,-1.0000,-1.0000,-1.0000,-1.0000,-1.0000,-1.0000,3.2500,40
```

## d2gl_perf_summary.jsonl
One JSON object per line, every `perf_summary_every` frames and once at shutdown. Schema:
perf_summary.schema.json. `game_frame_ms`, `render_interval_ms` and `gpu_ms` each carry
`count, fps, mean, median, p95, p99, max, variance, stalls_2x_median`. Percentiles are nearest-rank
over the window; variance is the population variance (ms^2); `fps = count * 1000 / sum(ms)`.
