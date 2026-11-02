# Warborn: World (design v1)

A living-world overhaul for Kenshi, built as an RE_Kenshi plugin with KenshiLib. **No direct control**: the player plays Kenshi normally, commands their band, and lives in a world that moves on its own. All mechanics, text and world events are **original**. Warborn doesn't copy another game's story, characters or UI art.

## Pillars
1. **The world has intentions.** Factions don't act at random. They have personalities, goals, grudges and alliances, and you can read their plans in rumours before they act.
2. **Small can become great.** A clan with one camp can win a war, take a city and become the strongest power.
3. **You matter.** Your band can tip a siege, earn the trust of factions, take contracts and gather followers.
4. **Grounded combat.** Skill means precision and good choices, never superhuman speed.

## Systems

### Factions and wars (World.cpp)
- **Personality:** aggression is fixed per faction, so the same faction always has the same temperament.
- **Choosing a war:** weighted by personality, size, grudges, and "small clan hunger".
- **Choosing a target:** revenge (retake a lost town), otherwise the best enemy town by distance, the owner's weakness and whether the owner is distracted.
- **Stages:** PLANNING (rumour), then MARCH (Kenshi's own `FactionWarMgr::triggerCampaign`), then OUTCOME.
- **Outcome:** chance = base × attacker strength / defender strength, ±6% per enemy the player felled for either side, ×0.6 for a defender's last town. A taken town changes owner (`TownBase::setFaction`).
- **Grudges:** the loser wants the town back for 90 minutes.
- **The strongest power** is announced when it changes.

### Reputation (People.cpp)
- "The enemy of my enemy": every enemy fighter the player's band fells gives a small amount of respect to every known faction at war with the victim's faction, capped per faction per 10 minutes.
- War help: fighting near a besieged town earns rep with the side you helped and costs some with the other.
- Contracts give larger rewards.

### Contracts (Contracts.cpp)
- **Hunt:** fell N fighters of a giver's enemy.
- **Defend:** a town of the giver is besieged; fight there and it must not fall.
- **Assault:** the giver attacks a town; help take it.
- Offers come only from factions that know you and aren't hostile. Up to 4 offers and 3 active contracts. Failing a war contract costs rep.

### Recruiting and commands (People.cpp)
- Select a person, then press **Y**. Chance = base + 0.6 × their faction's view of you, +15% for wanderers, ×0.6 for members of great powers. A refusal is remembered for 10 minutes.
- Commands go to your people near your selected character: follow, hold, charge, focus.

### Realistic combat (Combat.cpp)
- A speed cap on Kenshi's attack/block speed result, for everyone.
- Skill gap scales damage; armour exposure lets a skilled attacker find flesh.

### UI (Ui.cpp)
- Messenger line at the top of the screen.
- Journal window (J) with four tabs: News, Wars (running wars + power ranking), Contracts (Accept buttons), Reputation.
- Thread-safe: the main loop builds the text, the UI thread only shows it.

## Roadmap
- **Diplomacy:** truces after long wars, alliances against the strongest power.
- **Renown:** a personal fame score with titles, affecting recruiting.
- **Varied dialogue:** many original lines for messengers, rumours and recruit answers.
- **Saving:** the world state survives save/load.
- **Your own nation:** found a faction, hold towns, tax them, defend them.
- **Founding new settlements:** needs research into Kenshi's town creation.
- **FCS content:** original factions, characters, dialogue trees and quests, made in the FCS editor.

## Limits
- New 3D models and animations need artists. Animation mods from other authors work alongside Warborn.
- Brand-new cities aren't possible yet (no known safe way to create towns at runtime).
