# Car Driving Fix Plan

## Goal

Bring player driving closer to a responsive GTA-style arcade feel: predictable acceleration and braking, stable high-speed steering, immediate left/right agreement between input and wheel animation, and forgiving recovery from slides.

## Findings

- Input was read independently in steering, tire, and stability code. That made it easy for the subsystems to disagree about key state and steering direction.
- Steering and yaw stability are coupled to a custom rigid-body/tire model. Tuning one part can make the others fight it.
- Throttle and brake are binary, and forward/reverse selection is embedded in transmission behavior. Acceleration response is therefore hard to tune independently.
- Speed is exposed as unsigned horizontal magnitude, while several handling decisions need signed speed along the vehicle's forward axis.
- The single randomized CTest covers road-bed calculations; it does not exercise car handling or input direction.

## Refactor sequence

1. **Unify the driver input contract** — complete. Inputs are sampled once per physics step and shared by the tire and stability systems.
2. **Make speed state explicit** — complete. Signed forward speed is available, and gear selection plus brake/reverse throttle rules are centralized. Direction changes wait until the car is nearly stopped.
3. **Use a rigid-body library under custom vehicle forces** — implemented as the current physics foundation. Bullet integrates chassis motion, gravity, mass properties, lowered center of mass, inertia, and sampled terrain collision. Each wheel now raycasts against that terrain surface; custom suspension uses separate compression/rebound damping, axle anti-roll load transfer, and progressive bump stops. Tire slip/grip, per-surface grip presets, ABS, limited-slip torque bias, braking, wheel angular velocity, drivetrain, and grounded anti-flip recovery remain custom. The 120 Hz update now caps catch-up at eight steps per rendered frame, and terrain BVH recentering happens less often. Terrain prop collisions still use the existing terrain collision resolver.
4. **Align visuals and physics** — implemented in code. The shared left-positive steering input drives both front tire heading and wheel animation. Player body yaw is converted at the existing model boundary so left input turns left in world space.
5. **Make vehicle tuning data-driven** — implemented for the Renault 40 CV in `assets/cars/renault_40cv.json`. Mass, suspension damping/springs, anti-roll and bump stops, torque curve, ratios, torque split, differential lock, ABS, surface grip presets, anti-flip recovery, brakes, aero, stability assists, acceleration, and speed limits load without recompiling.
6. **Add handling regression coverage** — implemented. Tests cover steering shaping and transmission, custom dynamics, Bullet gravity/forward force/yaw and terrain raycasts, suspension load transfer/bump stops, ABS release, differential torque transfer, anti-flip thresholds/direction, surface grip presets, JSON tuning, and road-bed calculations.
7. **Tune in game** — still required. The rebuilt application starts and remains responsive, and the Renault configuration loads. Driving and visual wheel rotation still need verification at low, medium, and high speed.

## Acceptance checks

- Left input turns the visible front wheels and the car left; right input does the inverse.
- Releasing steering recenters smoothly and stability assists damp yaw on flat ground.
- Steering response decreases with speed but remains useful at road speeds.
- Acceleration, braking, and reverse transitions are predictable, without requiring an awkward stop-and-repress sequence.
- Build and automated regression checks pass. Full tire/suspension handling and in-game driving still need review at low, medium, and high speed.

## Remaining from the broader driving-system plan

- Tune the anti-flip recovery and surface grip values in-game.
- Replace the current terrain-zone-to-surface mapping with a painted surface ID map for road sections and material transitions.
- Add a gear/differential preset path for FWD, RWD, and AWD vehicles; the current drivetrain adds configurable open-to-limited-slip torque bias to the existing torque split.
- Replace the broad terrain zone grip lookup with per-surface queries at each tire.
- Add live physics debug drawing/telemetry so wheel loads, suspension travel, slip, and tire forces can be inspected while driving.
- Add more vehicle presets after the Renault handling is verified in-game.
- Damage, advanced camera/input/audio effects, traffic, pedestrian response, and world streaming remain later phases.
