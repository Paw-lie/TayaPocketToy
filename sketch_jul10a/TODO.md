# VTubeKram Pet - Implementation TODO

This file tracks progress and where each feature belongs in code.

## Legend

- [ ] Not started
- [~] In progress
- [x] Done

## 1) App flow and screens

- [x] Create two main screens: Pet and Menu
- [x] Add menu navigation: B1 left, B2 confirm, B3 right
- [x] Keep only one large centered option visible in menu

Where:
- App state and transitions: AppTypes.h, App.cpp
- Rendering: Renderer.cpp

## 2) Menu actions

- [x] Add menu actions: Feed, Pet, Play, DevInfo, Status, Lights toggle, ScreenBrightness, Clean, Back
- [x] Dynamic label for Lights ON/OFF
- [x] Show Clean only when available

Where:
- Action enums and state: AppTypes.h
- Action logic: App.cpp
- Labels and layout: Renderer.cpp

## 3) Splash behavior

- [x] Show splash_screen_taiyakisplash for 2 seconds on start
- [x] Use splash as fallback when animation is missing

Where:
- Boot timing and scene gate: App.cpp
- Fallback lookup: Assets.cpp
- Rendering overlay: Renderer.cpp

## 4) Character composition

- [x] Start each run with Blobb composition
- [x] Render base + accessory + face layers
- [x] Drive face by mood/action

Where:
- Character/profile state: AppTypes.h
- Runtime selection logic: App.cpp
- Layered draw order: Renderer.cpp
- Name-based asset lookup: Assets.cpp

## 5) Feeding and food preferences

- [x] Feed sub-menu for selecting food animation
- [x] Play food overlay with slower animation speed
- [x] Assign per-run likes/dislikes: 20/40/40
- [x] Track liked/neutral/disliked totals
- [x] Play mood reaction animation for 3 cycles after feeding

Where:
- Feeding flow and counters: App.cpp, AppTypes.h
- Food animation timing: App.cpp
- Overlay rendering: Renderer.cpp

## 6) Sleepiness and lights

- [x] Add Sleepiness stat
- [x] Increase with Feed/Play
- [x] Lights OFF triggers sleep behavior until recovered
- [x] Prevent sleep under blocking conditions

Where:
- Stat updates and constraints: App.cpp
- Persistent fields: AppTypes.h, Persistence.cpp
- Status view: Renderer.cpp

## 7) Dirt and Clean

- [x] Add poop logic: random after meals or forced after count threshold
- [x] Support small and big poop stages
- [x] Small poop blocks happiness gain and sleep
- [x] Big poop drains happiness and blocks sleep
- [x] Clean action appears only when needed

Where:
- Dirt state machine and probabilities: App.cpp
- Dirty-state fields: AppTypes.h
- Dirt rendering: Renderer.cpp

## 8) Evolution and death

- [x] Add evolution timer window and factor-based resolver
- [x] Add death conditions and revive/reset flow
- [x] Keep implementation pluggable for future characters

Where:
- Timers and decision policy: App.cpp
- Evolution/death state: AppTypes.h
- Placeholder revive animation: Renderer.cpp / Assets.cpp

## 9) Games (modular)

- [ ] Add game plugin interface
- [ ] Add placeholder game: 1..4 spinner, B2 stop, reward by result
- [ ] Add integration in Play menu action

Where:
- Interfaces and game state: AppTypes.h
- Registry and flow: App.cpp
- Game rendering: Renderer.cpp

## 10) Persistence and migration

- [ ] Save new fields (lights, sleepiness, food stats, dirt, progression)
- [ ] Bump save version and safe migration defaults

Where:
- Save struct and checksum: Persistence.cpp
- Save version: Config.h

## Immediate next coding slice

- [x] Implement two-screen menu architecture with dynamic menu entries and button mapping
- [x] Add boot splash timing and fallback lookup
