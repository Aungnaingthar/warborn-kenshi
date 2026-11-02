# Warborn: total conversion for Kenshi (design v0.3)

*Working name.* Warborn turns Kenshi into **its own game**: an original world played on three levels at once. It's built as an RE_Kenshi plugin on the open-source Direct Control mod (GPLv3, credited) plus FCS data for the world.

**The rule:** Warborn is inspired by the *feel* of grounded medieval RPGs (KCD 2-style realism) and Mount & Blade-style command. It **does not copy** any other game's characters, story, UI art or world. Everything in it is original.

## The three layers

| Layer | You are... | What you do |
|---|---|---|
| **1. Fighter** (direct control) | one character, in third person | Fight with the stance (lock-on, directional swings, block, dodge), with stamina, staggers and realistic speed |
| **2. Commander** (squad) | the leader of a warband | Give live orders in battle: follow, hold, charge, attack my target, formations, retreat |
| **3. Ruler** (strategy) | the head of your own faction | Run towns, economy, diplomacy and war on the world map; watch other factions expand, fight and fall |

You can switch layers any time: fight in the line, give orders mid-battle, then open the strategy screens.

## Status
| Feature | State |
|---|---|
| Skill + armor damage, hit-stop, camera shake | ✅ Built |
| Combat stance: lock-on, LMB attack / RMB block, directional swings | ✅ Built (testing: clicks not registering yet) |
| Realistic swing/block speed cap | ✅ Built |
| Staggers on strong hits, parry recoil | ✅ Built |
| Squad commands F5 to F8 | ✅ Built |
| Everything below | Planned |

## Roadmap (big picture)
1. **Fighter, finished:** stamina bar, combos, perks, ranged aiming, armor wear, weapon sharpening.
2. **Commander:** formations (line, shield wall, wedge), retreat and regroup, squad morale (troops break and run when losing), a command wheel UI, sergeants who relay orders.
3. **Life:** age and death, loyalty and betrayal, births, recruits who come and go, character backgrounds.
4. **Ruler:** your own faction screen (towns, population, food, taxes, guards), job board for citizens, trade caravans, diplomacy (alliances, tribute, war, peace).
5. **Living world:** factions war on each other, conquer towns, raid, send messengers with news; a news journal.
6. **Original world (FCS data):** new factions, towns, characters, dialogue, quests and a main story, written by Claude and placed in FCS by you.
7. **Art (needs artists):** new weapons, armor and props. You can learn Blender with Claude's help, or the project can bring in volunteer artists. Kenshi's own map stays (a new map is out of reach for now).

---

## PART 0: Character creation
- **Age slider** (16–70) in the character creator. It sets the starting age for the aging system. Young: learns faster, weaker. Old: stronger start skills, a shorter life left.
- **Background choice** (KCD 2-style origin): e.g. Ex-Slave, Farmer, Deserter, Noble's Child, Scavenger. Each gives starting skills, items and a small trait.
- **More face options:** an extended slider range and extra presets through the plugin.
- **New face/hair/beard models** need 3D art (Blender), which I can't make. We'll be **compatible with existing face/hair mods** instead, and add new presets on top. If you or a friend can model, I can wire the models in.

---

## PART 1: Melee combat (KCD 2 style, Kenshi version)

### Balance rules
- **No overpowered one-shots in normal fights.** Damage depends on **skill gap + armor**.
- A highly skilled fighter vs. an **unarmored** enemy: **1–2 hits** can kill or drop them.
- Equal skill, both armored: long, tense fights. Armor coverage and type really matter.
- A weak fighter vs. heavy armor: hits barely get through, so you need a blunt weapon or hits to unarmored parts.
- Everything is tunable in the `.ini`.

### Mechanics
| Mechanic | What it does | Kenshi hook (researched) |
|---|---|---|
| **Directional attacks** | Mouse direction picks the swing: overhead / left / right / thrust / upward | Kenshi has `CutDirection` (DOWNWARD, LEFT, RIGHT, THRUST, UPWARDS). Pick the technique that matches. *To verify in-game.* |
| **Stamina** | Swinging, blocking and dodging use stamina. At zero stamina you can't block and hit weakly. Regenerates when you stop. | New system, with a HUD bar |
| **Perfect block → Master Strike** | Block at the exact moment, then attack for a strong riposte | Extends Direct Control's perfect block |
| **Armor zones** | Hits on unarmored body parts do much more damage | `Damages` (cut / blunt / pierce / armourPenetration) in the hit hook |
| **Weapon roles** | Blunt beats heavy armor, cutting beats light armor, thrusts pierce | Damage-type multipliers |
| **Skill-based damage** | Weapon skill, Strength and Dexterity scale damage and speed; Toughness reduces stagger | `CharStats` fields |
| **Combos** | Certain direction sequences (e.g. right → left → overhead) become a combo with bonus damage. Unlocked by perks. | Our own input tracking |

### Impact feel (hits feel real)
- **Hit-stop:** a tiny freeze on contact, longer on heavy hits.
- **Camera shake** scaled to damage.
- **Hit reactions:** the enemy staggers and flinches, using Kenshi's own **per-body-part stumble animations** (`getStumbles(bodypart)`).
- **Knockback / knockdown** on heavy blunt hits.
- Extra blood and sparks on hard hits; sparks when you hit armor.

## PART 2: Ranged combat (crossbows, turrets)
- Aim manually with the third-person crosshair.
- **Aim sway** gets smaller with Crossbow skill and bigger when you're tired or hurt.
- Headshots / weak spots do more damage, and armor still matters.
- Reload is skill-based (already in Kenshi: `getReloadSkill`).

## PART 3: Progression (KCD 2 perks + Kenshi skills)
- Kenshi skills still level by use, as vanilla.
- **Perks:** every 5 levels in a skill, you choose a perk for that skill. Examples:
  - Katana 20: "Iaido" (the first strike from sheath does more damage)
  - Blunt 30: "Bone Breaker" (heavy hits can cripple limbs)
  - Dodge 25: "Light Feet" (dodging costs less stamina)
  - Crossbow 40: "Steady Hand" (less sway)
- Perk window on `K`.

## PART 4: Weapon smithing (KCD 2-style forge)
- At a weapon bench you play a **forging minigame**: heat the metal, hammer to the right shape, quench.
- Your result + Weapon Smithing skill = the **quality** of the weapon.
- Good forging can produce better-than-normal grades; bad forging produces junk.
- Hook: intercept Kenshi's crafting and set the item's quality.

## PART 5: Living characters
- **Age:** every character has an age and ages over in-game time. Old age lowers Strength/Dex, and from about 70 there's a rising chance of **death from old age**. (Faces don't visibly age.)
- **Loyalty:** recruits have loyalty. It goes up with good treatment (food, pay, winning) and down with starving, losing fights, or being left in danger.
- **Betrayal:** low-loyalty recruits may steal and leave, or turn on you mid-fight.
- **Random recruits:** wanderers show up and ask to join; some are spies or traitors.
- **Births:** settled towns (yours and NPC ones) slowly gain **new young adults (age ~16)**, so populations regrow. Kenshi has no child models, so people appear as young adults.

## PART 6: Your nation
- **Citizens:** people join your town as citizens, not squad members. You don't control them directly.
- **Command board:** assign jobs (farmer, miner, guard, crafter, hauler) and they do them on their own.
- Population needs food and beds; happy towns grow, starving towns lose people.

## PART 7: Living world
- **Faction wars:** AI factions attack **each other**, not just you: Holy Nation vs. Shek, United Cities vs. rebels, hives, and so on.
- **Conquest:** a town that loses a war can be taken over and become the winner's town.
- **Expansion:** strong factions send settlers to set up new outposts. (Very hard. This needs research into how Kenshi spawns towns, so the first version might only take over existing places.)
- **Town criers / messengers:** news of wars, conquests, famines and big events reaches you through a messenger NPC who finds you, plus a news popup.

## PART 8: Gaps filled (things a mod this big also needs)

**Fairness and combat world**
- **Enemies use the same rules:** NPCs also have stamina, directional attacks and perfect blocks. Otherwise the combat is fake.
- **Morale:** enemies who are losing (allies down, badly hurt) may **flee or surrender**. Surrendered enemies can be robbed, captured or let go.
- **Combat trainers:** masters in towns teach you combos and perks for money (as in KCD 2).

**Equipment**
- **Weapon durability and sharpening:** blades dull with use; a grindstone minigame restores the edge (KCD 2).
- **Armor damage and repair:** armor wears down in fights and needs repair at a bench.

**Aging, family, nation**
- **Succession:** when your leader dies of old age, an heir or chosen successor takes over, so the game continues as a dynasty.
- **Diplomacy:** alliances, peace treaties, tribute, declaring war, so faction wars aren't random chaos.
- **Nation economy:** taxes from citizens, trade caravans, upkeep for guards.
- **Crime and witnesses:** stealing or attacking with witnesses raises your bounty and lowers town reputation. Unseen crimes stay hidden (Kenshi already has bounties; we add witnesses).
- **News journal:** a log of every crier/messenger report, so you can read past events.

**Mod quality**
- **Modules:** split into separate plugins (Combat, Survival/Life, Nation, World) so players can turn parts on or off, and bugs in one part don't break the others.
- **In-game settings + difficulty presets** (Story / Realistic / Hardcore).
- **Save-safe:** our data lives in its own files; removing the mod mustn't break saves.
- **Performance:** the world simulation (wars, births, aging) runs on timers, not every frame, so the game stays smooth.
- **Compatibility:** works with Direct Control-style controls, face/race mods and popular animation mods.

---

## Build order
| # | Part | Why this order |
|---|---|---|
| 0 | Toolchain: the fork builds and loads | Nothing works without this |
| 1 | **Melee combat + impact feel** | Core of the mod |
| 2 | Stamina + directional attacks + Master Strike | KCD 2 feel |
| 3 | Perks | Progression |
| 4 | Ranged | Builds on the combat code |
| 5 | Age + loyalty + betrayal + random recruits | Character systems |
| 6 | Smithing minigame | Self-contained |
| 7 | Citizens + command board | Big |
| 8 | Faction wars + conquest + messengers + births | Biggest; needs the most research |

## Honest limits
- No new animations: we reuse Kenshi's swings, stumbles and knockdowns, but control timing, damage and reactions.
- No children models or visible aging.
- Parts 7–8 are very large. Some things (factions founding brand-new cities) may only be partly possible.
- I can't run Kenshi here. You build and test, send me errors and logs, and I fix the code. A project this size takes **months**, built part by part.
