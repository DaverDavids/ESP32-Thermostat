
Found both bugs. Here's the full diagnosis:

***

## Bug 1 — Center Button Short Press Never Fires

**Root cause:** The `BTN_PENDING` → `BTN_HELD` transition fires after just **30ms** (`DEBOUNCE_MS`), which is nearly instant. So every time you press the center button and hold it even slightly, it immediately transitions to `BTN_HELD` and the short-press release path (`if (!low)` in `BTN_PENDING`) **never executes** — the button is already in `BTN_HELD` by the time you lift your finger.

The short-press logic lives only in `BTN_PENDING`'s `if (!low)` branch. Once it's in `BTN_HELD`, releasing just resets the state — no action taken.

**Fix:** Add a minimum press duration before transitioning to `BTN_HELD`. Change the transition condition in `BTN_PENDING` from:

```cpp
// CURRENT — fires after only 30ms (debounce), instantly promotes to HELD
} else if (now - btns[i].pendingSince >= DEBOUNCE_MS) {
  btns[i].phase = BTN_HELD;
  btns[i].nextFire = now + (i == 2 ? CTR_LONGPRESS_MS : RAMP_DELAY_MS);
```

To:

```cpp
// FIXED — for center button, require full RAMP_DELAY_MS (200ms) before promoting to HELD
// UP/DN still promote after DEBOUNCE_MS for responsive setpoint ramping
} else if (now - btns[i].pendingSince >= (i == 2 ? RAMP_DELAY_MS : DEBOUNCE_MS)) {
  btns[i].phase = BTN_HELD;
  btns[i].nextFire = now + (i == 2 ? CTR_LONGPRESS_MS : RAMP_DELAY_MS);
```

This gives a clean ~170ms window where releasing center registers as a short press. Long-press still works because `nextFire` is set 2000ms out from when `BTN_HELD` is entered.

***

## Bug 2 — E-Stop Counts All Buttons, Not Just Center

**Root cause:** `registerPress()` is called for **every button** (`i == 0, 1, 2`) on every short press release.  So pressing UP + DN + CENTER rapidly (3 presses any combination) triggers E-stop. That's why it seems random or involves UP sometimes — it actually does.

**Fix:** Only call `registerPress()` for the center button. In `BTN_PENDING`'s `if (!low)` block:

```cpp
// CURRENT
btns[i].phase = BTN_IDLE;
registerPress();  // ← called for all 3 buttons
if (i == 2 && !stopLatched) {
```

```cpp
// FIXED
btns[i].phase = BTN_IDLE;
if (i == 2) registerPress();  // ← center button only
if (i == 2 && !stopLatched) {
```


***

## Relay Polarity

**Yes, it changed.** The current code is:

```cpp
// outputOn=true  -> PIN_MOSFET LOW  (active-low load)
// outputOn=false -> PIN_MOSFET HIGH
#define MOSFET_WRITE(on) digitalWrite(PIN_MOSFET, (on) ? LOW : HIGH)
```

This is **active-low** — the pin goes LOW to energize the load. If your relay module has an **active-high** input (most common blue relay boards need HIGH to trigger), it will never fire. Flip it:

```cpp
// Active-high relay: HIGH = energized
#define MOSFET_WRITE(on) digitalWrite(PIN_MOSFET, (on) ? HIGH : LOW)
```

If you're unsure which your relay needs, you can test quickly: with the device running, manually short PIN 3 to 3.3V and see if the relay clicks. If yes → active-high → flip the macro. The comment on that line was there from v1 when a MOSFET with a pull-down gate was used; the relay board may have an onboard transistor that inverts again, so test first before assuming.

