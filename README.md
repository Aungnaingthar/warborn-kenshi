# Warborn (Kenshi overhaul mod)

A Kingdom Come: Deliverance 2-style overhaul for Kenshi: grounded, skill-and-armor-based melee and ranged combat with heavy impact feel, perks, a smithing minigame, aging and loyalty, your own nation, and a living world where factions war with each other.

Full plan: [`docs/DESIGN.md`](docs/DESIGN.md)

**Status:** Phase 0. The base code is imported; we're getting the build working.

## Credits and license

Warborn is built on **[Direct Control for Kenshi](https://github.com/smokefoolius/Kenshi-Direct-Control)** by **smokefoolius** (v1.5.0, upstream commit `dc809fc`). It supplies the WASD movement, the first- and third-person cameras, and manual attack/block/dodge input. Their original README is kept in [`upstream/`](upstream/DirectControl_README.md).

Built with [KenshiLib](https://github.com/BFrizzleFoShizzle/KenshiLib) and [RE_Kenshi](https://www.nexusmods.com/kenshi/mods/847) by BFrizzleFoShizzle.

Like Direct Control and KenshiLib, Warborn is licensed under the **GNU GPLv3** (see `LICENSE`).

## Building

1. Install **Visual Studio 2019/2022** (Desktop development with C++) **and the Visual C++ 2010 x64 compiler** (VS2010 + SP1). KenshiLib plugins must be built with the 2010 compiler. Setup guide: https://github.com/weisspure/re_kenshi-working-solution/blob/main/README.md
2. Clone this repo and the dependencies **side by side**:
   ```
   C:\KenshiMods\warborn-kenshi
   C:\KenshiMods\KenshiLib_Examples_deps   (git clone https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps)
   ```
3. Open `WASDCombatPlugin.sln`, choose **Release | x64**, and build. (Debug is broken in KenshiLib.)
4. Copy the built `WASDCombatPlugin.dll` and `WASDCombatPlugin.ini` into `Kenshi/mods/WASDCombatPlugin/`. Also copy the `materials` folder from the official Direct Control release zip (it holds the first-person shader, and it isn't in the source repo).
5. **Turn off the original Direct Control** (Workshop or manual) while testing, because two copies can't run at once.

The project and DLL are renamed to Warborn after the first successful build, once we know the toolchain works.
