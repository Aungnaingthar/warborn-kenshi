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
#include <vector>

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
    // warband (command-only missions)
    int   warbandKeys[4];       // add/remove, take a place, patrol, return
    bool  playerConquest;       // a place your warband clears becomes yours
    int   townTax;              // cats per town you hold, every 10 minutes
    // training
    int   trainKey;             // T = spar, Shift+T = tournament
    float sparDamage;           // damage multiplier in a spar
    float sparSeconds;
    float sparXp;
    int   yardKey;              // Alt+Y: mark / remove the training yard
    float yardRadius;
    float yardRestSeconds;
    float partnerXp;            // per ~6 s, two people training at dummies together
    float soloDummyXp;          // per ~6 s, alone at a dummy
    float partnerRange;         // how close the two dummies must be
    // followers
    int   talkKey;              // H
    int   marryKey;             // M
    float yearMinutes;          // real minutes per year of age
    bool  oldAgeDeath;
    bool  betrayal;
    float betrayalRate;
    bool  betrayalDesert;       // betrayers may leave for their old faction
    float smithingXpMult;       // easier smithing
    float learnMult;            // all skills
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
// The person you clicked: a selected stranger first (Kenshi keeps clicked
// NPCs in the selected-objects list), otherwise your selected squad member.
Character*  wbSelectedPerson();
// True while c is one of the player's people (pointer comparison only - safe
// to call with a character that may already be gone).
bool        wbIsOwned(Character* c);
Character*  wbPersonUnderMouse(float radiusPx);   // Pick.cpp
std::string wbFactionName(Faction* f);
std::string wbTownName(TownBase* t);
int         wbTownCount(Faction* f);
bool        wbIsPlayerFaction(Faction* f);
float       wbRand01();

// ------------------------------------------------------------ lines (Lines.cpp)
enum LineKind { LINE_RUMOUR = 0, LINE_WAR_START, LINE_TOWN_FELL, LINE_TOWN_HELD, LINE_STRONGEST,
                LINE_TRUCE, LINE_COALITION, LINE_RECRUIT_YES, LINE_RECRUIT_NO, LINE_RECRUIT_HOSTILE,
                LINE_NEW_TITLE, LINE_COUNT };
struct WbVars { std::string A, D, T, H, R, N, F, X; };
std::string wbLine(int kind, const WbVars& v);   // a random original line, placeholders filled

// ------------------------------------------------------------ renown (People.cpp)
void        wbAddRenown(float amount, const char* why);
float       wbRenown();
const char* wbRenownTitle();

// ------------------------------------------------------------ saving (Persist.cpp)
void wbPersistInstall(const char* modFolder);   // hooks Kenshi's save
void wbPersistLoad();                           // after a save is loaded
std::vector<std::string> wbSplit(const std::string& line);   // tab-separated fields
std::string wbClean(const std::string& s);                   // strip tabs/newlines
Faction* wbFindFaction(const std::string& name);
void wbWorldSave(std::vector<std::string>& out);     void wbWorldLoad(const std::vector<std::string>& in);
void wbPeopleSave(std::vector<std::string>& out);    void wbPeopleLoad(const std::vector<std::string>& in);
void wbContractsSave(std::vector<std::string>& out); void wbContractsLoad(const std::vector<std::string>& in);
void wbUiSave(std::vector<std::string>& out);        void wbUiLoad(const std::vector<std::string>& in);

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
std::string wbRealmSummary();
// Territory map (main thread): every town as a dot in its owner's colour, and
// a legend (MyGUI colour tags) of each power, its wars and its pacts.
struct WbMapDot { float x, z; unsigned int rgb; bool besieged, player; std::string town; };
void wbWorldMap(std::vector<WbMapDot>& dots, std::string& legend);

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
void wbBattleInstall(const char* iniPath);    // fair battles: spread attackers across enemies
// Any thread: scale a melee hit by skill gap and armour (everyone, NPCs too).
void wbCombatBeforeHit(Character* victim, Character* attacker, Damages& damage);

// ------------------------------------------------------------ warband (Warband.cpp)
void wbWarbandFrame();
void wbWarbandReset();
std::string wbWarbandSummary();
void wbWarbandSave(std::vector<std::string>& out);   void wbWarbandLoad(const std::vector<std::string>& in);

// ------------------------------------------------------------ training (Training.cpp)
void wbTrainingInit();
void wbTrainingFrame();
void wbTrainingReset();
std::string wbTrainingSummary();
void wbTrainingSave(std::vector<std::string>& out); void wbTrainingLoad(const std::vector<std::string>& in);
// Any thread: blunt the blows between two sparring partners.
void wbTrainingScaleHit(Character* victim, Character* attacker, Damages& damage);

// ------------------------------------------------------------ followers (Followers.cpp)
void  wbFollowersInstall();
void  wbFollowersFrame();
void  wbFollowersReset();
void  wbFollowerJoined(Character* c, Faction* origin, bool prisoner);
float wbRecruitBonus();
float wbContractPayMult();
void  wbFollowersGlory(float renown);
void  wbFollowersPaid(int cats);
void  wbFollowersChooseOrigin(int which);
bool  wbOriginChosen();
const char* wbOriginName(int i);
std::string wbOriginSummary();
std::string wbFollowersSummary();
void  wbFollowersSave(std::vector<std::string>& out); void wbFollowersLoad(const std::vector<std::string>& in);
void  wbWarbandAdd(Character* c);
std::string wbLatestNews();                           // newest news line, no time stamp
// pay the player's purse (contracts, trade); false if they can't afford a cost
void  wbPayPlayer(int cats);
bool  wbChargePlayer(int cats);
int   wbPlayerMoney();
void  wbTakeMoneyByForce(int cats);

// ------------------------------------------------------------ contracts
void wbContractsFrame();
void wbContractsReset();
void wbContractsOnFelled(Faction* victimFaction);
std::string wbContractsSummary();             // offers + active, numbered
bool wbContractsAccept(int index);            // index into the offer list
int  wbContractsOfferCount();
