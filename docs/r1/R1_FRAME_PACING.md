# R1 frame pacing

- Simulation timing untouched: D2 still ticks at 25 Hz; extra displayed frames come from the existing
  d2fps/interpolation path, not from R1.
- Limiter (`foreground_fps` pacing / waitable timer) unchanged; it runs after present on the render thread.
- VSync on (90 Hz Deck OLED or 60 Hz): pacing is set by present; `early_slot_release` only lets the game
  thread overlap the fence, it does not change when frames are presented.
- VSync off: the game thread may produce frames slightly sooner; the bounded slot queue and limiter cap it.
- Expected effect (unmeasured): lower `game_wait_ms` on fast frames, possibly lower input latency by up to
  the fence time. No effect expected on slow (game-CPU-bound) frames.
