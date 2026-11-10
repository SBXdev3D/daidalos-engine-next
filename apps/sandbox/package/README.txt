Daidalos Engine Next - sandbox
==============================

A first-person shooter set in a dark underground factory, built on Daidalos Engine Next.

Requirements
------------

- Windows 10 or 11, 64-bit, or Linux x86-64 with a C library at least as recent as Ubuntu 24.04's.
- A GPU and driver with Vulkan 1.4 and hardware ray tracing (VK_KHR_acceleration_structure and
  VK_KHR_ray_query): NVIDIA RTX 30 series, AMD RDNA 2, Intel Arc A or newer. The sandbox refuses to
  start, with a message, on a GPU without them.

Playing
-------

Start sandbox (sandbox.exe on Windows). It looks for assets/ in the working directory, then beside the
executable.

  Mouse, right stick        Look
  W, A, S, D, left stick    Move
  Shift                     Run
  Space                     Jump
  C, left Ctrl              Crouch
  W on a ladder             Climb up, or down when looking down
  Left button, right trig.  Fire
  R, right shoulder         Reload
  1, 2, Q, wheel, D-pad up  Change weapons
  E                         Use a door
  F                         Flashlight
  F5                        Restart
  Tab                       Free the mouse
  Escape                    Pause

Settings
--------

daidalos.cfg holds the engine's settings and sandbox.cfg the game's, each commented. The options menu
saves the player's choices to user.cfg; delete it to go back to the defaults. The command line overrides
them all with +name value, for example:

  sandbox +window.fullscreen true

The sandbox writes sandbox.log, and on Windows crash dumps to crashdumps/, in the working directory.

Licences
--------

The engine and the sandbox: CC BY-NC-SA 4.0, in LICENSE.
The level: assets/Factory/LICENSE.md. The fonts: SIL Open Font License 1.1, in assets/Fonts/OFL.txt.
The libraries the engine is built with: THIRD_PARTY_NOTICES/.
