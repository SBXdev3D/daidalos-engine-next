# ADR-0085: Combat feel

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Step 16.4 of the showcase (ADR-0082). `docs/DEMO.md` asks for hit feedback, a screen flash on damage, and stronger recoil and sway. The game had a red flash on a wound and gamepad kicks (ADR-0076). It had no way to tell where a blow came from or whether a shot had hit, and no recoil.

## Decision

- **Where a wound came from:** an arc of red marks around the crosshair, toward the attacker relative to where the player looks, for 1.2 s after each blow. It comes on top of the red flash, which says only that it hurt.
- **Hit markers:** four marks around the crosshair for 0.18 s when a shot wounds a zombie, red when it kills.
- **Recoil:** each shot kicks the view up, 1.6° for the handgun and 4.5° for the shotgun. The kick springs back with a time constant of about a ninth of a second. The aim is the view, so quick shots climb a little, as they should.
- **Flinch:** a wounded zombie stops walking for 0.3 s. It is a stagger the player can read and use, not a stun.
- **Not done:**
  - Breathing that follows exertion: the project has no breathing sound, and synthesizing one convincingly is its own work.
  - Weapon sway: already follows the walk (ADR-0058), and stays.

## Consequences

- The numbers are first guesses, tuned in play in Phase 17.
- The direction marker is drawn with axis-aligned squares, as the canvas draws no rotated shapes. It reads as an arc of dots.

## Alternatives considered

- **A red vignette on the side of the wound:** softer, and harder to read at a glance in the dark. The marks are where the eyes are.
- **Hit sounds:** the impacts by surface already sound on flesh (ADR-0067).
