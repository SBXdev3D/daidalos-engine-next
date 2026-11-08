---
title: "Why I'm rebuilding my engine"
type: introduction
status: draft
images: [sandbox-phase-3.png]
---

# Why I'm rebuilding my engine

Hello, and welcome to the development diary of Daidalos Engine Next.

I'm SBX, a software engineer for more than 14 years. Daidalos is a real-time 3D engine that I write myself in C++ back in 2011, so this is not a new idea of mine. "Next" is the point of this project: I'm rebuilding it from the ground up on modern foundations.

## Where Daidalos comes from

In 2008, I got interested in game development and decided to learn 3D programming. To learn, I took inspiration from one of my favorite games, Doom, and made it my first project: Daidalos, an old-school first-person shooter mock-up in C++ and OpenGL, in the spirit of Doom and Wolfenstein 3D. You are trapped in labyrinthine levels and fight your way through the creatures that haunt them, picking up what helps you on the way. It was fully 3D, used OpenGL without shaders, SDL and a BSP tree, and I coded everything myself.

In 2010, I learned advanced 3D programming at university: rasterization, DirectX 9, shaders. So I pushed the project further and rewrote it with DirectX 9: Daidalos DX, with dynamic lights and soft shadows, normal mapping and a BSP tree.

In 2011, I wanted to go further still: why not make my own 3D engine? I wanted to learn everything an engine does: file loading, textures, sound, animation, dynamic lighting, collisions, user interface. That became Daidalos Engine, a DirectX 9 engine that took everything from Daidalos DX and made it generic: forward and deferred rendering, HLSL shaders for dynamic lights and soft shadows, BSP collisions, a particle engine for smoke and fire, and the beginnings of a level editor. I kept working on it for several years, until time ran out: I wasn't a student anymore, but a software engineer.

## Why now

For years after that, I wanted to get back to the project but couldn't find the time. Then came AI. In 2026, I decided to resume it, but it had aged: old C++, an old graphics library. With what I've learned since, and with the help of Claude Code, I decided to redo everything, more modern. That's Daidalos Engine Next.

My goal is not to make an Unreal Engine clone, I'm only doing this as a pure tech lover and a passionate about 3D technologies. 

![The engine today: a few cubes, a free camera and a debug overlay](../images/sandbox-phase-3.png)

*This is the engine today. It isn't much to look at yet, but everything between the keyboard and that picture is mine or was chosen by me, and it all runs on Vulkan 1.3.*

## What I'm building

Daidalos Engine Next targets indoor 3D games, in first or third person. That focus matters. I'm not trying to rebuild Unreal or Unity. Corridors, rooms and doors allow a much smaller engine than open worlds, and a smaller engine is one I can actually finish.

The goal is an engine that covers everything a complete game needs: rendering, physics, audio, input, a user interface, gameplay systems and development tools. It should run on Windows and Linux.

## The constraint that shapes everything

I'm building this alone.

That single fact drives most of my technical decisions. An idea that is exciting but costs months of maintenance gets rejected. When a mature library solves a well-understood problem, I use it: SDL3 for windows today, Jolt for physics and miniaudio for sound later. I keep for myself the parts I want to understand deeply and control, such as the rendering layer and the entity system.

## How I work: me and Claude Code

I develop this engine with Claude Code, Anthropic's AI coding assistant, and I want to be upfront about it.

Here is how it works in practice. Before each phase, I review a plan and the technical choices behind it. I've asked Claude to challenge my decisions rather than simply go along with them, and nothing starts until I approve it. Claude then does much of the implementation and writes the tests, and the code has to build and pass them. The architecture stays my responsibility, and I have to be able to understand and maintain everything that lands in the repository.

Every lasting decision is written down in a short Architecture Decision Record, so I can always find out why something is the way it is. There are 15 so far.

I'll be showing you how this works in practice, including where it helps and where it doesn't.

## What you'll find here

- **Architecture posts**, explaining how the engine works and why, with diagrams.
- **Progress posts**, at the end of each development phase, with screenshots and honest numbers.
- **Roadmap updates**, when the plan changes.

I'll also tell you what doesn't work. Unfinished and unverified parts are clearly marked. A diary that only shows the good days isn't much use to anyone.

Next up: a tour of how the engine fits together.
