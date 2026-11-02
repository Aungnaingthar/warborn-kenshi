# Warborn (Kenshi overhaul mod)

A Kingdom Come: Deliverance 2-style overhaul for Kenshi: grounded, skill-and-armor-based melee and ranged combat with heavy impact feel, perks, a smithing minigame, aging and loyalty, your own nation, and a living world where factions war with each other.

Full plan: [`docs/DESIGN.md`](docs/DESIGN.md)

**Status**
- `main` branch: Phase 0, the Direct Control base (builds and loads).
- `phase1-combat` branch: Warborn combat. All settings are in the `[Warborn]` section of `WASDCombatPlugin.ini`.
  - Skill/armor damage for every fighter, hit-stop and camera shake (`Warborn.cpp`)
  - KCD 2 combat stance: lock-on, mouse attack/block, directional swings
  - Realistic speed cap, so high skill never swings superhumanly fast
  - No name/health bars over enemies

## Controls (Warborn combat stance)

| Key | Action |
|---|---|
| `R` | Enter / leave the combat stance (turns on Direct Control, manual combat and the action camera, and locks the nearest enemy) |
| Left click | Attack |
| Right click | Block: hold to guard, tap as a hit lands for a perfect block |
| Move the mouse | Choose the swing: up = overhead, left, right, down = rising; keep still = thrust |
| `TAB` | Lock the next enemy |
| `Q` | Dodge |
| `W A S D` | Move / circle the locked enemy |
| `Alt` (hold) | Free cursor |

All other Direct Control keys still work (see `upstream/DirectControl_README.md`).

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
4. Double-click `Install-to-Kenshi.bat`. It copies the mod into `Kenshi\mods\Warborn` (backing up your old `.ini`).
5. In the Kenshi launcher's **Mods** tab, tick **Warborn**. **Turn off the original Direct Control** if you have it, because two copies can't run at once.
