# AsapGuard lecture-room final runbook

Use this checklist only with an instructor-approved, ventilated demonstration area. The ESP32 controller—not the delayed dashboard—is the immediate fan-control path.

## 1. Inspect and start

- [ ] Put the assembly on a nonconductive surface.
- [ ] Check every wire, divider junction, relay terminal, and fan connection.
- [ ] Confirm no exposed conductor can touch the miniature.
- [ ] Confirm both warm MQ cans and relay contacts have clearance from miniature material.
- [ ] Connect ESP32 USB power first and verify a normal boot.
- [ ] Connect external regulated 5 V power.
- [ ] Verify common ground, LCD output, and no boot loop.
- [ ] Allow MQ sensors to warm for several minutes.
- [ ] If a fresh baseline is needed, press RST/EN while leaving MQ heater power on.
- [ ] Add no stimulus until `baselineSamples` is 20 and `baselineReady` is true.
- [ ] Confirm `controlMode` is `AUTO_RULES`, state is `NORMAL`, and both fans are off.

## 2. Calibrate MQ-6 digital sensitivity

- [ ] In clean air, slowly turn the MQ-6 module potentiometer toward the DOUT transition.
- [ ] When the DOUT LED changes, back off until it is just off and stable.
- [ ] Keep all flames and ignition sources away.
- [ ] From 20–30 cm away, use one very brief unlit lighter-gas tap.
- [ ] Stop immediately when DOUT changes; never release continuously or use an enclosure.
- [ ] Verify active-low `mq6Triggered:true`, immediate Stage 2, and both fans on.
- [ ] Allow full recovery and confirm the signal returns false.
- [ ] If it will not trigger safely, stop real-gas testing and simulate MQ-6 by briefly pulling the GPIO35 divider junction to ground. Never connect GPIO35 to 5 V.

## 3. Install into the miniature

- [ ] Disconnect external 5 V first, then USB.
- [ ] Mount the LCD on the right where all four rows remain visible.
- [ ] Mount both fans on the left with unobstructed blades and airflow.
- [ ] Mount DHT22, MQ-135, and MQ-6 on top with open airflow.
- [ ] Keep the DHT22 away from direct MQ-heater heat and direct fan exhaust.
- [ ] Secure the ESP32, dividers, transistor stages, and signal wiring to a nonconductive base.
- [ ] Keep fan current wiring short, secure, and out of the blade paths.
- [ ] Keep bare relay contacts and screw-terminal strands inaccessible and separated.
- [ ] Repeat the startup inspection after mounting.

## 4. Final demonstration

- [ ] Restart shortly before presenting so the demo completes within the current 60-minute SAS-token window.
- [ ] Confirm a new Storage batch appears and `/api/latest` advances.
- [ ] Normal: show stable readings, `NORMAL`, and both fans off.
- [ ] Humidity: hold a squeezed damp tissue near DHT22; stop at Stage 1/Fan 1.
- [ ] Recovery: remove stimulus and wait for the safety hold and return to normal.
- [ ] Smoke/VOC: use a small amount of vape or extinguished-candle smoke; keep active flame at least 50 cm away.
- [ ] Show predictive trend before/alongside the direct threshold and Fan 1 response.
- [ ] Recovery: ventilate and wait for stable normal readings.
- [ ] Gas: use the already calibrated brief test, or the safe GPIO35-to-ground simulation.
- [ ] Show `mq6Triggered`, Stage 2, and both fans on.
- [ ] Confirm the dashboard is described as roughly one minute delayed.
- [ ] End by removing external 5 V first, then USB.

Stop the test immediately for a boot loop, hot/damaged wiring, loose strands, unexpected relay behavior, persistent gas odor, or unsafe sensor values.
