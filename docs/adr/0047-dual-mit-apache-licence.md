# ADR-0047: Dual MIT and Apache 2.0 licence

- **Status:** Accepted
- **Date:** 2026-11-14
- **Supersedes:** ADR-0013

## Context

ADR-0013 put the project under CC BY-NC-SA 4.0 so that others could not use it commercially. The owner now wants the engine open to any use, including commercial use by others. Creative Commons does not recommend its licences for software: they have no terms for source distribution or patents. Every commit is the owner's, so the owner can relicense without asking anyone.

## Decision

- **The project is dual-licensed under MIT or Apache 2.0, at the user's option**, the usual choice for open-source engines and libraries. Apache 2.0 adds an explicit patent grant. MIT stays compatible with GPLv2, which Apache 2.0 is not.
- **The licence texts are `LICENSE-MIT` and `LICENSE-APACHE`**, at the root. They replace `LICENSE`. The release archive includes both.
- **Every source file declares `SPDX-License-Identifier: MIT OR Apache-2.0`** in the header defined in `docs/CONVENTIONS.md`.
- **The owner's own assets use the same licence.** These are the factory's geometry, animations, normal maps, `handgun_ammo_d.png`, the particle atlas, the HUD images and the sky. One licence covers the whole repository, so a game made with the engine can ship the demo's content too.
- **Third-party assets keep their own terms.** These are the factory's free textures and sounds, the `Source/` maps whose origin is not recorded yet, the Rajdhani fonts (SIL OFL 1.1) and the downloaded sample models. `assets/Factory/LICENSE.md` lists them.
- **Contributions are dual-licensed the same way**, unless the contributor says otherwise.

## Consequences

- Anyone can use, modify and sell the engine and games made with it, provided they keep the copyright notice. Copies already published under CC BY-NC-SA 4.0 stay under that licence. The change cannot be taken back for copies published after it.
- **The NVIDIA RTX SDKs are no longer excluded by the licence.** Section 4(e) of the NVIDIA RTX SDKs License only forbids putting the SDK under a licence that requires its source to be disclosed or allows derivative works of it. A permissive engine that keeps the SDK as a separate dependency does not do that. ADR-0028 and ADR-0041 relied on that conflict. Their decisions stay: the denoiser, the radiance cache and ReSTIR are built from the papers and measured, and they run on every vendor's ray-tracing GPU. DLSS Super Resolution and Ray Reconstruction may be considered later, as optional vendor-specific paths behind the existing seams (the upscaling interface, ADR-0039's denoiser inputs). Using one would need its own decision.
- All dependencies use permissive licences (zlib, MIT, BSD, Apache 2.0, public domain) and are compatible.

## Alternatives considered

- **Keep CC BY-NC-SA 4.0** (ADR-0013): it blocks commercial use by others, which the owner no longer wants.
- **MIT only**: shorter, but it has no patent grant.
- **Apache 2.0 only**: it is incompatible with GPLv2 projects.
- **CC BY-NC-SA 4.0 or CC BY 4.0 for the owner's assets**: a licence written for art, but two licences in one repository. The owner chose one licence for everything.
