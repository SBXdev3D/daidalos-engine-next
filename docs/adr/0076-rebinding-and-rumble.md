# ADR-0076: Rebinding, gamepad menus and rumble

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 15 (ADR-0041) lists "input: full rebinding, gamepad rumble and trigger effects tied to tension". The sandbox's controls were fixed in code (`MakeActions`), its menus answered only the keyboard and mouse, so a player on a gamepad could not leave the pause menu, and nothing reached a gamepad's motors.

## Decision

- **Names for buttons** (`input/Bindings.hpp`): every key, mouse button and gamepad button has a name that reads back, as config files hold it: the key's position name (`W`, `LeftShift`, `Digit1`, `Key123` for keys without one), `MouseLeft`, `PadSouth`. `FirstPressed` returns the first press of a device in a frame, for a menu that waits for the button to bind. `platform::KeyLabel` gives what the keyboard's layout prints on a key (SDL's key name), so menus show AZERTY's `Z` where the binding is `W`.
- **Bindings are settings:** each control the player can bind has two string cvars, `bind.key.<id>` and `bind.pad.<id>`, saved in `user.cfg` like any option. The game makes its `ActionMap` from them (`sandbox::MakeActions(cvars)`) at the start and again when one changes. A value that does not name a button of its device keeps the default; an empty one unbinds. The engine's `ActionMap` is unchanged: what a game lets the player bind is the game's choice.
- **What is bound, and what is not:** sixteen controls, the four movement sides each on its own, on the keyboard or mouse and on the gamepad. The sticks, the fire trigger, Escape, Tab, F5, the console's key and the gamepad's Start, Back and Guide stay fixed, and cannot be taken.
- **Binding a taken button swaps:** the control that had it gets the binding the rebound control had, so no button does two things and none is lost.
- **The options' Controls tab** lists the controls in two columns, the key and the gamepad button side by side; using a cell waits for the next press of its device (Escape or Start leave it). `ui::Context::Hold` keeps the widgets from reading that press. A button resets every binding.
- **Gamepads in menus:** `ui::Context` takes the D-pad or the left stick as arrows (a lean of the stick moves once) and South as Enter; East goes back, as Escape does.
- **Rumble:** `input::RumbleMixer` mixes pulses that fade (a shot, a wound) over a level the game holds, and says when the level is worth a message to the pad: a change of 0.04, stillness, or half the second each message lasts. The application sends it after each `OnUpdate` with `Platform::RumbleGamepads`, scaled by `input.rumble`, stops every pad at shutdown, and the sandbox stops it behind menus. The sandbox pulses the handgun lightly and the shotgun heavily, a wound and death; under tension, or at low health, the heavy motor beats a heartbeat, 60 to 130 a minute and harder as the danger grows.

Tested: every button's name reads back, names ignore case, the first press of the right device is the one taken (`BindingsTest`); pulses fade and add up to 1, levels are sent when they change or before they lapse, stillness once (`RumbleTest`); the D-pad, stick and South drive the widgets, and held widgets keep their focus (`ContextTest`); defaults bind each control to its own key, a taken key swaps, reserved keys change nothing, a movement side rebinds alone, a wrong setting keeps the default (`ControlsTest`). Seen: the Controls tab in English and French on an AZERTY keyboard, waiting for a key, then Jump on E and Use on Space.

## Consequences

- Rumble and the gamepad menus are tested without a pad; no pad was at hand to feel them, so their strengths are first guesses for a player to tune with `input.rumble`.
- Key names are SDL's, in English, past letters and signs (`Space`, `F6`); a translated name per key is left for later.
- Only one key and one gamepad button per control.

## Alternatives considered

- **Bindings as one file of their own:** a second format to read and save; settings already do both, and the console can change them.
- **Trigger effects (DualSense resistance):** SDL passes them only as Sony's raw report bytes, per pad model, and there was no such pad to try them on. Left out until there is.
- **A message to the pad every frame:** some drivers send each one over the wire; the mixer sends a few a second.
