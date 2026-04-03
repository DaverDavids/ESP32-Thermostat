<img src="https://r2cdn.perplexity.ai/pplx-full-logo-primary-dark%402x.png" style="height:64px;margin-right:32px"/>

# I thought we changed it so it didn't need to move back down to the setpoint to continue to the next ramps.... once it is losing heat it is equalized. also we are past the soaking stage I think, it turned the heater back on, but it still says soaking and still says the target is 200c - it's above that and climbing. still says step 1 out of 6

Now I have the full picture. I can see two clear bugs:

1. **`RS_SOAKING` never advances `rampStep`** — the `isStable()` branch inside `RS_SOAKING` does `rampStep++` and transitions to `RS_HEATING`, but `setpoint` is never updated to track the current step target. The display and `/rampstatus` use `activeProfile.stepTargets[safeStep]` and `rampStep` correctly — but the **heater bang-bang inside `RS_SOAKING/RS_OVERSHOOT_WAIT`** uses the literal variable `stepTarget` which is computed at the TOP of `rampControlLoop()` before the switch. That's fine. The real issue: after `RS_OVERSHOOT_WAIT`, the code has `currentTemp <= stepTarget + 5.0f` as a gate — but `stepTarget` here is **still the current step's target** (e.g. 200°C), and `currentTemp` is 200+22=222°C, so `222 <= 205` is **false** — it never exits. The temp must actually cool to within 5°C of the setpoint before it proceeds. This contradicts the intended behavior (you said once it's losing heat it's equalized).
2. **`setpoint` global is never updated** to the current ramp step — so the OLED and web UI setpoint always shows the manually set value (200°C in your case), not the ramp step target.

**Fixes:**

- `RS_OVERSHOOT_WAIT`: Remove the `currentTemp <= stepTarget + 5.0f` gate entirely. Once stable (falling, plateau detected), advance regardless of how far above target we still are. Add a simple "temp is falling or plateaued" check instead.
- `RS_SOAKING`/`RS_OVERSHOOT_WAIT`/`RS_HEATING`: Sync `setpoint` global to the current `stepTarget` so the display and UI always reflect the active ramp target.
- The stability check during `RS_SOAKING` is fine but needs the same removal of the proximity gate so it can trigger when temp is stable around an overshoot plateau.

