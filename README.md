# Warborn: World (Kenshi mod)

A living-world overhaul for Kenshi. Factions have personalities and **plan** their wars: rumours spread, armies march, towns fall and change hands, and small clans can rise to become the mightiest power in the land. You earn reputation, take contracts, recruit people and command your band. Combat is realistic: skill makes fighters precise, never superhumanly fast.

There's no direct control: you play Kenshi as usual and give orders.

Full plan: [`docs/DESIGN.md`](docs/DESIGN.md)

## Branches
- `warborn-world`: **this mod** (current work).
- `main`: the earlier Direct Control-based base, kept for reference.
- `phase1-combat`: the earlier combat experiments.

## Features (v0.3)

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
| **Fair battles** | No more 10-on-1 dogpiles: at most 2 attackers per target, and the rest spread out over the enemy line. |
| **Diplomacy** | Factions worn out by war agree truces; when one power grows too big, its enemies form a coalition against it. |
| **Renown** | Personal fame with eight titles, announced by messengers. Makes people more willing to join you. |
| **Warband** | People you command with missions, not control: take a town or camp (it becomes yours), patrol, return. |
| **Territory map** | Journal Map tab: every town in its owner's colour, sieges marked, each power's wars and pacts. |
| **Saved with your game** | News, renown, pacts, grudges, contracts and your warband are stored per save. |
| **Original dialogue** | Many varied lines for rumours, war news, conquests, pacts and recruit answers. |
| **Training** | **T**: two of your people spar 1 vs 1 (blunted blows, real bruises and knock-downs) and learn faster. **Shift+T**: a knockout tournament, the champion gains fame. |
| **Start options** | Journal **People** tab: choose Noble, Merchant, Warrior, Exile or Slave-born once per game, each with its own money, standing, fame and loyalty effects. |
| **Talk to anyone** | **H** on the selected person: greetings by how their faction sees you, rumours, what they think of your fame, work. Your own people tell you how they feel. |
| **Traits, bond, loyalty, betrayal** | Each follower has a trait, a bond with you, an origin and an age. People of your own land stay loyal; low loyalty can mean theft, desertion or escape, depending on the trait. |
| **Two kinds of recruits** | **Y** joins your squad (you control them). **Shift+Y** joins your warband (you only command them). |
| **Prisoners who work** | **Y** on a knocked-out person takes them prisoner. They work like anyone else but try to escape until they trust you, then they stay for good. |
| **Trade** | Contracts pay cats. Caravan contracts: invest now, get your share back with profit if their towns stay safe. |
| **Age and marriage** | Everyone ages; old fighters weaken and can die of old age. **M** marries two of your people (more loyal together; they grieve if one dies). |
| **Easier smithing** | Smithing skills learn 3× faster (adjustable). |
| **Rank and vassals** | Your power grows with towns and fame: Wandering Band, Free Company, Lordship, Realm, Kingdom. From Lordship on, small friendly factions may swear fealty, pay tribute and call for help, and they rebel if they stop liking you. |

## Controls

| Key | Action |
|---|---|
| `J` | Journal: News, Wars, Contracts (with Accept buttons), Reputation, Map, People (with Origin buttons) |
| `Y` | Recruit the selected person |
| `F5` | Your people near your selected character: follow me |
| `F6` | Hold position |
| `F7` | Charge (each attacks the enemy nearest to them) |
| `F8` | Focus (everyone attacks the enemy nearest your selected character) |
| `B` | Add / remove the selected person to / from your warband |
| `U` | Warband: take the nearest enemy town or camp |
| `I` | Warband: patrol around your selected character |
| `O` | Warband: return to your selected character |
| `T` | Spar: the selected fighter trains 1 vs 1 with the nearest squadmate |
| `Shift+T` | Tournament among your nearby fighters |
| `H` | Talk to the selected person (press again for the next topic) |
| `M` | Marry the selected person to the nearest unmarried one of your people |
| `Shift+Y` | Recruit into the warband instead of the squad |

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
