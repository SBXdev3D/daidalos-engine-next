# ADR-0004: Platform layer with SDL3

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

The engine needs windowing, events, and keyboard, mouse, and gamepad input on Windows and Linux, and must create Vulkan surfaces.

## Decision

- SDL3 is the platform layer. SDL2 is not used.
- SDL3 is wrapped by concrete engine classes (`Window`, `EventQueue`, input translation) that hide SDL behind a private implementation (pimpl).
- No `IPlatform` virtual interface is introduced: SDL3 is already the portability layer, and a second abstraction would have only one implementation.

## Consequences

- SDL types never appear in public engine headers.
- Tests use the SDL dummy video driver.
- If SDL is replaced one day, only the platform module changes.

## Alternatives considered

- Native Win32 plus X11/Wayland code: much more code to write and maintain.
- GLFW: less complete gamepad support and no audio; SDL3 covers more of the engine's needs.
- SDL3 GPU API as the rendering layer: rejected in ADR-0007.
