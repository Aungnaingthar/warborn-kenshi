// Warborn: World - a living-world overhaul for Kenshi.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// Shared declarations between the modules:
//   Main.cpp       plugin entry, hooks, frame loop, keys, config
//   World.cpp      faction ambitions, planned wars, conquest, the strongest power
//   People.cpp     reputation, recruiting, squad commands, "felled" tracking
//   Contracts.cpp  contracts (quests) offered by factions
//   Ui.cpp         messenger toasts and the journal window
//
// MUST compile with the VS2010 (v100) toolset: no range-for, no initializer
// lists, no std::min/max (Windows.h macros), no enum names starting WB_
// (Windows.h owns some of those).
#pragma once

#include <string>

class Character;
class Faction;
class TownBase;
class GameWorld;

// ------------------------------------------------------------ settings
struct WbConfig
{
    bool  log;
    // world
    bool  wars;
    float warIntervalMin;       // real minutes between new war plans
    int   maxWars;              // plans running at once
    float planMin;              // "gathering forces" before the march
    float resolveMin;           // march -> outcome
    bool  conquest;             // towns can change owner
    float conquestBase;         // base chance an assault takes the town
    bool  attackPlayerTowns;
    // reputation
    float repPerFelled;
    float repCapPerFaction;     // per 10 minutes
    float repWarHelp;
    // recruiting
    int   recruitKey;
    float recruitBase;
    // commands
    int   cmdKeys[4];
    float cmdRange;
    // ui
    int   journalKey;
};
extern WbConfig g_wb;

void wbLoadConfig(const char* iniPath);
void wbLog(const char* text);                 // debug log, only if Log=true
void wbLogAlways(const char* text);

// ------------------------------------------------------------ time
// Real minutes the world has been running unpaused since the save loaded.
double wbWorldMinutes();

// ------------------------------------------------------------ helpers
Faction*    wbPlayerFaction();
std::string wbFactionName(Faction* f);
std::string wbTownName(TownBase* t);
int         wbTownCount(Faction* f);
bool        wbIsPlayerFaction(Faction* f);
float       wbRand01();

// ------------------------------------------------------------ ui
void wbToast(const char* text);               // one line at the top for a few seconds
void wbNews(const char* text);                // toast + journal entry
void wbUiFrame();                             // UI thread, once per frame
void wbUiMainFrame();                         // main thread, once per frame (builds journal text)
void wbUiReset();
void wbJournalToggle();

// ------------------------------------------------------------ world
void wbWorldFrame();                          // main thread, once per frame
void wbWorldReset();
// The player's squad felled a member of `victimFaction` near `pos` (x,z).
void wbWorldOnFelled(Faction* victimFaction, float x, float z);
// For the journal: one line per running war.
std::string wbWorldSummary();
std::string wbPowerRanking();
std::string wbDiplomacySummary();

// War events the contracts listen to.
struct WbWarEvent
{
    int         id;
    Faction*    attacker;
    Faction*    defender;
    TownBase*   town;
    std::string attackerName, defenderName, townName;
    bool        conquered;
    int         playerHelpedAttacker;          // fighters of the defender the player felled
    int         playerHelpedDefender;          // fighters of the attacker the player felled
};
void wbContractsOnWarStarted(const WbWarEvent& e);
void wbContractsOnWarEnded(const WbWarEvent& e);

// ------------------------------------------------------------ people
void wbPeopleFrame();                         // main thread, once per frame
void wbPeopleReset();
// The player's squad felled (knocked out / killed) someone of `victimFaction` at (x,z).
void wbPeopleOnFelled(Faction* victimFaction, float x, float z);
std::string wbReputationSummary();
void wbGiveRep(Faction* f, float amount, const char* why);

// ------------------------------------------------------------ combat (realism)
class Damages;
void wbCombatInstall(const char* iniPath);    // reads settings, installs the speed hook
// Any thread: scale a melee hit by skill gap and armour (everyone, NPCs too).
void wbCombatBeforeHit(Character* victim, Character* attacker, Damages& damage);

// ------------------------------------------------------------ contracts
void wbContractsFrame();
void wbContractsReset();
void wbContractsOnFelled(Faction* victimFaction);
std::string wbContractsSummary();             // offers + active, numbered
bool wbContractsAccept(int index);            // index into the offer list
int  wbContractsOfferCount();
