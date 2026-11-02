# Warborn: World (Kenshi mod)

A living-world overhaul for Kenshi. Factions have personalities and **plan** their wars: rumours spread, armies march, towns fall and change hands, and small clans can rise to become the mightiest power in the land. You earn reputation, take contracts, recruit people and command your band. Combat is realistic: skill makes fighters precise, never superhumanly fast.

There's no direct control: you play Kenshi as usual and give orders.

Full plan: [`docs/DESIGN.md`](docs/DESIGN.md)

## Branches
- `warborn-world`: **this mod** (current work).
- `main`: the earlier Direct Control-based base, kept for reference.
- `phase1-combat`: the earlier combat experiments.

## Features (v0.1)

| | |
|---|---|
| **Planned wars** | Every faction has its own personality. They pick goals (revenge, opportunity, ambition), rumours spread while they gather forces, then they launch Kenshi's own war campaigns against enemy towns. |
| **Conquest** | Assaults can take a town, which changes owner. The loser holds a grudge and tries to take it back. Small clans are the hungriest. |
| **Messengers** | News of rumours, wars, sieges and conquests pops up at the top of the screen and is kept in the journal. |
| **Reputation** | Felling a faction's enemies earns their respect ("the enemy of my enemy"). Fighting at a besieged town helps decide the battle and earns gratitude from the side you helped. |
| **Contracts** | Hunts, defending towns and joining assaults, offered by factions that know you. Rewards in reputation. |
| **Recruiting** | Select anyone and press **Y**. Your standing with their faction decides whether they join. |
| **Commands** | **F5** follow me, **F6** hold position, **F7** charge, **F8** focus the nearest enemy. |
| **Realistic combat** | A speed cap for everyone, with skill turned into precision (skill gap and armour exposure scale damage). |

## Controls

| Key | Action |
|---|---|
| `J` | Journal: News, Wars, Contracts (with Accept buttons), Reputation |
| `Y` | Recruit the selected person |
| `F5` | Your people near your selected character: follow me |
| `F6` | Hold position |
| `F7` | Charge (each attacks the enemy nearest to them) |
| `F8` | Focus (everyone attacks the enemy nearest your selected character) |

Every key and number is in `Warborn.ini`.

## Building

1. Visual Studio 2019/2022 (Desktop development with C++) **plus the Visual C++ 2010 x64 compiler**. Guide: https://github.com/weisspure/re_kenshi-working-solution/blob/main/README.md
2. Clone side by side:
   ```
   C:\KenshiMods\warborn-kenshi
   C:\KenshiMods\KenshiLib_Examples_deps
   ```
   and run `Setup.bat` in `KenshiLib_Examples_deps` once.
3. Open **`Warborn.sln`**, choose **Release | x64**, then **Build → Rebuild Solution**.
4. Double-click `Install-to-Kenshi.bat`, then tick **Warborn** in the Kenshi launcher's Mods tab.

## Credits and license

Built with [KenshiLib](https://github.com/BFrizzleFoShizzle/KenshiLib) and [RE_Kenshi](https://www.nexusmods.com/kenshi/mods/847) by BFrizzleFoShizzle. The earlier branches build on [Direct Control for Kenshi](https://github.com/smokefoolius/Kenshi-Direct-Control) by smokefoolius.

Licensed under the **GNU GPLv3** (see `LICENSE`), like KenshiLib.
