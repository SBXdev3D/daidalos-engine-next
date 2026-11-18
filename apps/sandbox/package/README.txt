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
  P, right stick click      Photo mode
  F12                       Screenshot, into Screenshots/
  Tab                       Free the mouse
  Escape                    Pause

Keys and buttons can be changed in Options > Controls.

Settings
--------

daidalos.cfg holds the engine's settings and sandbox.cfg the game's, each commented. The options menu
saves the player's choices to user.cfg; delete it to go back to the defaults. The command line overrides
them all with +name value, for example:

  sandbox +window.fullscreen true

The sandbox writes sandbox.log (the previous run's stays as sandbox.previous.log), and on Windows crash
dumps to crashdumps/, in the working directory. After a crash, the next start writes a readable report beside
the dump (crashdumps/<name>.dmp.txt) and the menu says so. Nothing is sent anywhere: attach the report to a bug
report if you wish.

Licences
--------

The engine and the sandbox: MIT or Apache 2.0, at your option, in LICENSE-MIT and LICENSE-APACHE.
The level: assets/Factory/LICENSE.md. The fonts: SIL Open Font License 1.1, in assets/Fonts/OFL.txt.
The libraries the engine is built with: THIRD_PARTY_NOTICES/.
