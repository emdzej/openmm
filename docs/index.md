---
layout: home

hero:
  name: OpenMM
  text: Micro Machines, reimplemented
  image:
    src: /hero.svg
    alt: "Two toy Formula One cars racing across a pool table between pool balls, playing cards and a cube of chalk, under the OpenMM wordmark"
  tagline: "A faithful reimplementation of Micro Machines (Codemasters, 1994) for DOS, in C11, ported function by function from the original program. It runs the whole game from your own floppy, as one WebAssembly module for the gasm runtime, natively and in the browser, and is checked against the original routine by routine and picture by picture."
  actions:
    - theme: brand
      text: Status
      link: /guide/status
    - theme: alt
      text: Get started
      link: /guide/

features:
  - title: Faithful
    details: Not an emulator and not a remake. The goal is the original's behaviour, its handling, its computer drivers and its quirks, ported from the hand-written assembly of MICRO.EXE and checked against it in an x86 emulator.
  - title: Your own data
    details: Reads your floppy, its disk image or the installed game. Even the tables inside the original program are read from your copy at run time. Nothing from the game is in OpenMM.
    link: /guide/game-data
    linkText: Game data
  - title: Runs on gasm
    details: One openmm.wasm for macOS, Linux, Windows and the browser on the gasm WebAssembly game runtime, drawing the original's 320 x 200 VGA screen at 4:3.
    link: /guide/status
    linkText: Status
---
