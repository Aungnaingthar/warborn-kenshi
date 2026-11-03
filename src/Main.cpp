// Warborn: World - plugin entry, hooks, frame loop, keys, settings.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/Faction.h>
#include <kenshi/Town.h>
#include <kenshi/FactionWarMgr.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/InputHandler.h>
#include <kenshi/CameraClass.h>
#include <kenshi/Damages.h>
#include <kenshi/combat/CombatClass.h>
#include <ogre/OgreVector3.h>
#include <core/Functions.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

WbConfig g_wb;
static HMODULE s_module = NULL;

// ===================================================================
// logging
// ===================================================================
void wbLogAlways(const char* text) { DebugLog(text); }
void wbLog(const char* text) { if (g_wb.log) DebugLog(text); }

// ===================================================================
// helpers shared by the modules
// ===================================================================
Faction* wbPlayerFaction()
{
    return (ou && ou->player) ? ou->player->getFaction() : NULL;
}
bool wbIsPlayerFaction(Faction* f) { return f && f == wbPlayerFaction(); }
std::string wbFactionName(Faction* f)
{
    if (!f) return std::string("Drifters");
    return f->getName();
}
std::string wbTownName(TownBase* t)
{
    if (!t) return std::string("somewhere");
    return t->getName();
}
int wbTownCount(Faction* f)
{
    if (!f || !f->warMgr) return 0;
    return (int)f->warMgr->myTowns.size();
}
float wbRand01() { return (float)rand() / (float)RAND_MAX; }
bool wbIsOwned(Character* c)
{
    if (!c || !ou || !ou->player) return false;
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (uint32_t i = 0; i < mine.size(); ++i) if (mine[i] == c) return true;
    return false;
}

Faction* wbFindFaction(const std::string& name)
{
    if (!ou || !ou->factionMgr || name.empty()) return NULL;
    const lektor<Faction*>* all = ou->factionMgr->getAllFactions();
    for (uint32_t i = 0; all && i < all->size(); ++i)
        if ((*all)[i] && (*all)[i]->getName() == name) return (*all)[i];
    return NULL;
}

// ===================================================================
// world clock: real minutes running unpaused since the save loaded
// ===================================================================
static double    s_worldMinutes = 0.0;
static ULONGLONG s_lastTick     = 0;
double wbWorldMinutes() { return s_worldMinutes; }

// ===================================================================
// settings ([Warborn] in Warborn.ini next to the DLL)
// ===================================================================
static float iniF(const char* path, const char* key, float d)
{
    char def[32], buf[64];
    sprintf_s(def, sizeof(def), "%g", d);
    GetPrivateProfileStringA("Warborn", key, def, buf, sizeof(buf), path);
    return (float)atof(buf);
}
static bool iniB(const char* path, const char* key, bool d)
{
    char buf[16];
    GetPrivateProfileStringA("Warborn", key, d ? "true" : "false", buf, sizeof(buf), path);
    for (char* p = buf; *p; ++p) *p = (char)tolower((unsigned char)*p);
    if (!strcmp(buf, "true") || !strcmp(buf, "1") || !strcmp(buf, "yes") || !strcmp(buf, "on"))  return true;
    if (!strcmp(buf, "false") || !strcmp(buf, "0") || !strcmp(buf, "no") || !strcmp(buf, "off")) return false;
    return d;
}
// Key names: a letter or digit ("J"), F1..F24, or TAB / SPACE / HOME / END / INSERT / DELETE.
// Key names: "ALT+R", "CTRL+F9", "SHIFT+K" or a plain key. Keys: a letter or
// digit, F1..F24, TAB / SPACE / HOME / END / INSERT / DELETE.
// Encoded as vk | 0x100 (Alt) | 0x200 (Ctrl) | 0x400 (Shift).
static const int WBKEY_ALT = 0x100, WBKEY_CTRL = 0x200, WBKEY_SHIFT = 0x400;
static int keyFromName(const char* s)
{
    char k[32]; int n = 0;
    for (const char* p = s; *p && n < 31; ++p) if (!isspace((unsigned char)*p)) k[n++] = (char)toupper((unsigned char)*p);
    k[n] = 0;
    int mods = 0;
    char* key = k;
    for (;;)
    {
        if (!strncmp(key, "ALT+", 4))   { mods |= WBKEY_ALT;   key += 4; continue; }
        if (!strncmp(key, "CTRL+", 5))  { mods |= WBKEY_CTRL;  key += 5; continue; }
        if (!strncmp(key, "SHIFT+", 6)) { mods |= WBKEY_SHIFT; key += 6; continue; }
        break;
    }
    int len = (int)strlen(key), vk = 0;
    if (len == 1 && (isalpha((unsigned char)key[0]) || isdigit((unsigned char)key[0]))) vk = key[0];
    else if (key[0] == 'F' && len >= 2 && len <= 3) { int f = atoi(key + 1); if (f >= 1 && f <= 24) vk = VK_F1 + f - 1; }
    else if (!strcmp(key, "TAB"))    vk = VK_TAB;
    else if (!strcmp(key, "SPACE"))  vk = VK_SPACE;
    else if (!strcmp(key, "HOME"))   vk = VK_HOME;
    else if (!strcmp(key, "END"))    vk = VK_END;
    else if (!strcmp(key, "INSERT")) vk = VK_INSERT;
    else if (!strcmp(key, "DELETE")) vk = VK_DELETE;
    return vk ? (vk | mods) : 0;
}
static int iniKey(const char* path, const char* key, const char* d)
{
    char buf[32];
    GetPrivateProfileStringA("Warborn", key, d, buf, sizeof(buf), path);
    int vk = keyFromName(buf);
    return vk ? vk : keyFromName(d);
}

void wbLoadConfig(const char* path)
{
    g_wb.log               = iniB(path, "Log",               true);
    g_wb.wars              = iniB(path, "Wars",              true);
    g_wb.warIntervalMin    = iniF(path, "WarIntervalMin",    6.0f);
    g_wb.maxWars           = (int)iniF(path, "MaxWars",      3.0f);
    g_wb.planMin           = iniF(path, "PlanMin",           2.0f);
    g_wb.resolveMin        = iniF(path, "ResolveMin",        8.0f);
    g_wb.conquest          = iniB(path, "Conquest",          true);
    g_wb.conquestBase      = iniF(path, "ConquestBase",      0.35f);
    g_wb.attackPlayerTowns = iniB(path, "AttackPlayerTowns", false);
    g_wb.repPerFelled      = iniF(path, "RepPerFelled",      0.6f);
    g_wb.repCapPerFaction  = iniF(path, "RepCapPerFaction",  8.0f);
    g_wb.repWarHelp        = iniF(path, "RepWarHelp",        12.0f);
    g_wb.recruitKey        = iniKey(path, "RecruitKey",      "ALT+R");
    g_wb.recruitBase       = iniF(path, "RecruitBase",       0.25f);
    g_wb.cmdKeys[0]        = iniKey(path, "CmdFollowKey",    "F5");
    g_wb.cmdKeys[1]        = iniKey(path, "CmdHoldKey",      "F6");
    g_wb.cmdKeys[2]        = iniKey(path, "CmdChargeKey",    "F7");
    g_wb.cmdKeys[3]        = iniKey(path, "CmdFocusKey",     "F8");
    g_wb.cmdRange          = iniF(path, "CommandRange",      150.0f);
    g_wb.journalKey        = iniKey(path, "JournalKey",      "ALT+J");
    g_wb.warbandKeys[0]    = iniKey(path, "WarbandAddKey",    "ALT+B");
    g_wb.warbandKeys[1]    = iniKey(path, "WarbandTakeKey",   "ALT+U");
    g_wb.warbandKeys[2]    = iniKey(path, "WarbandPatrolKey", "ALT+P");
    g_wb.warbandKeys[3]    = iniKey(path, "WarbandReturnKey", "ALT+O");
    g_wb.playerConquest    = iniB(path, "PlayerConquest",    true);
    g_wb.townTax           = (int)iniF(path, "TownTax",      250.0f);
    g_wb.trainKey          = iniKey(path, "TrainKey",        "ALT+T");
    g_wb.talkKey           = iniKey(path, "TalkKey",         "ALT+H");
    g_wb.marryKey          = iniKey(path, "MarryKey",        "ALT+M");
    g_wb.yearMinutes       = iniF(path, "YearMinutes",       60.0f);
    g_wb.oldAgeDeath       = iniB(path, "OldAgeDeath",       true);
    g_wb.betrayal          = iniB(path, "Betrayal",          true);
    g_wb.betrayalRate      = iniF(path, "BetrayalRate",      1.0f);
    g_wb.betrayalDesert    = iniB(path, "BetrayalDesert",    true);
    g_wb.smithingXpMult    = iniF(path, "SmithingXpMult",    3.0f);
    g_wb.learnMult         = iniF(path, "LearnMult",         1.0f);
    if (g_wb.yearMinutes < 1.0f) g_wb.yearMinutes = 1.0f;
    g_wb.sparDamage        = iniF(path, "SparDamage",        0.35f);
    g_wb.sparSeconds       = iniF(path, "SparSeconds",       90.0f);
    g_wb.sparXp            = iniF(path, "SparXp",            3.0f);
    g_wb.yardKey           = iniKey(path, "YardKey",         "ALT+Y");
    g_wb.yardRadius        = iniF(path, "YardRadius",        120.0f);
    g_wb.yardRestSeconds   = iniF(path, "YardRestSeconds",   40.0f);
    g_wb.partnerXp         = iniF(path, "PartnerXp",         1.5f);
    g_wb.soloDummyXp       = iniF(path, "SoloDummyXp",       0.4f);
    g_wb.partnerRange      = iniF(path, "PartnerRange",      200.0f);
    if (g_wb.maxWars < 1) g_wb.maxWars = 1;
    if (g_wb.warIntervalMin < 0.5f) g_wb.warIntervalMin = 0.5f;
    if (g_wb.resolveMin < 1.0f) g_wb.resolveMin = 1.0f;

    char b[256];
    sprintf_s(b, sizeof(b), "[Warborn] config wars=%d every=%.1fmin maxWars=%d conquest=%d base=%.2f recruitKey=%d journalKey=%d",
              g_wb.wars ? 1 : 0, g_wb.warIntervalMin, g_wb.maxWars, g_wb.conquest ? 1 : 0,
              g_wb.conquestBase, g_wb.recruitKey, g_wb.journalKey);
    DebugLog(b);
}

// ===================================================================
// keys (main thread, edges)
// ===================================================================
static bool kenshiForeground()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
static bool s_keyPrev[256];
static bool s_keyNow[256];
// Pressed this frame: the key went down, with exactly the Alt/Ctrl it was
// bound with (Shift only checked when the binding asks for it - Shift+key
// variants such as the tournament are read separately).
static bool keyPressed(int code)
{
    int vk = code & 0xFF;
    if (vk <= 0 || vk > 255) return false;
    if (!(s_keyNow[vk] && !s_keyPrev[vk])) return false;
    bool alt = s_keyNow[VK_MENU], ctrl = s_keyNow[VK_CONTROL], shift = s_keyNow[VK_SHIFT];
    if (((code & WBKEY_ALT) != 0) != alt) return false;
    if (((code & WBKEY_CTRL) != 0) != ctrl) return false;
    if ((code & WBKEY_SHIFT) && !shift) return false;
    return true;
}
static void snapshotKeys()
{
    memcpy(s_keyPrev, s_keyNow, sizeof(s_keyNow));
    for (int vk = 1; vk < 256; ++vk) s_keyNow[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
}
// read by People.cpp
bool g_wbRecruitPressed = false;
bool g_wbCmdPressed[4]  = { false, false, false, false };
bool g_wbWarbandKeys[4] = { false, false, false, false };
bool g_wbTrainPressed = false, g_wbTournamentPressed = false, g_wbYardPressed = false;
bool g_wbTalkPressed = false, g_wbMarryPressed = false, g_wbRecruitShift = false;
static volatile bool s_journalTogglePending = false;

static void pollKeys()
{
    snapshotKeys();
    bool fg = kenshiForeground();
    bool textInput = gui && gui->inDialogue();
    g_wbRecruitPressed = keyPressed(g_wb.recruitKey) && fg && !textInput;
    for (int i = 0; i < 4; ++i) g_wbCmdPressed[i] = keyPressed(g_wb.cmdKeys[i]) && fg;
    for (int i = 0; i < 4; ++i) g_wbWarbandKeys[i] = keyPressed(g_wb.warbandKeys[i]) && fg && !textInput;
    bool shift = s_keyNow[VK_SHIFT];
    bool train = keyPressed(g_wb.trainKey) && fg && !textInput;
    g_wbTrainPressed = train && !shift;
    g_wbRecruitShift = shift;
    g_wbTalkPressed  = keyPressed(g_wb.talkKey) && fg && !textInput;
    g_wbMarryPressed = keyPressed(g_wb.marryKey) && fg && !textInput;
    g_wbTournamentPressed = train && shift;
    g_wbYardPressed = keyPressed(g_wb.yardKey) && fg && !textInput;
    if (keyPressed(g_wb.journalKey) && fg && !textInput) s_journalTogglePending = true;
}

// ===================================================================
// world-live gate + reset on load
// ===================================================================
static bool s_wasLive = false;
static void drainFelled();       // below, with the hit hook
static bool worldLive()
{
    return ou && ou->player && !ou->isLoadingFromASaveGame();
}
static void resetAll(const char* why)
{
    wbWorldReset();
    wbPeopleReset();
    wbContractsReset();
    wbWarbandReset();
    wbTrainingReset();
    wbFollowersReset();
    wbUiReset();
    s_worldMinutes = 0.0;
    char b[96]; sprintf_s(b, sizeof(b), "[Warborn] reset (%s)", why); DebugLog(b);
}

// ===================================================================
// hooks
// ===================================================================
static void (*s_mainLoopOrig)(GameWorld*, float) = NULL;
static void mainLoop_hook(GameWorld* thisptr, float time)
{
    s_mainLoopOrig(thisptr, time);

    bool live = worldLive();
    if (live != s_wasLive)
    {
        s_wasLive = live;
        if (!live) resetAll("world unloading");
        else { resetAll("world loaded"); wbPersistLoad(); }
        s_lastTick = GetTickCount64();
    }
    if (!live) return;

    ULONGLONG now = GetTickCount64();
    if (s_lastTick == 0) s_lastTick = now;
    ULONGLONG dt = now - s_lastTick;
    s_lastTick = now;
    if (dt > 1000) dt = 1000;                       // a hitch or a long pause screen
    if (!ou->isPaused()) s_worldMinutes += (double)dt / 60000.0;

    pollKeys();
    drainFelled();
    wbWorldFrame();
    wbPeopleFrame();
    wbContractsFrame();
    wbWarbandFrame();
    wbTrainingFrame();
    wbFollowersFrame();
    wbUiMainFrame();
}

// While Alt is held, Kenshi's own hotkeys are swallowed, so Alt+R / Alt+B /
// Alt+M ... are Warborn's alone (and don't also open crafting, build, map...).
static void (*s_processKeysOrig)(GameWorld*) = NULL;
static void processKeys_hook(GameWorld* thisptr)
{
    if (key && (GetAsyncKeyState(VK_MENU) & 0x8000) && kenshiForeground()) key->events.clear();
    s_processKeysOrig(thisptr);
}

// UI must be touched from the render/UI thread: the player camera update.
static void (*s_cameraUpdateOrig)(CameraClass*, bool) = NULL;
static void cameraUpdate_hook(CameraClass* thisptr, bool controlEnabled)
{
    s_cameraUpdateOrig(thisptr, controlEnabled);
    if (!ou || !ou->player || thisptr != ou->player->camera) return;
    if (s_journalTogglePending) { s_journalTogglePending = false; wbJournalToggle(); }
    wbUiFrame();
}

// Every melee hit in the world. Combat may run on a worker thread, so this
// only (1) applies the realistic-combat damage rules and (2) QUEUES "the
// player's squad felled someone of faction X here"; the main loop handles it.
struct FelledEvent { Faction* faction; float x, z; };
static CRITICAL_SECTION s_felledLock;
static FelledEvent      s_felled[64];
static int              s_felledN = 0;

static HitMaterialType (*s_iHitOrig)(CombatClass*, CutDirection, Damages&, Character*) = NULL;
static HitMaterialType iHit_hook(CombatClass* cc, CutDirection dir, Damages& damage, Character* who)
{
    Character* victim = cc ? cc->me : NULL;
    bool wasDown = victim && (victim->isDead() || victim->isUnconcious() || victim->isDown());
    if (victim && who) { wbCombatBeforeHit(victim, who, damage); wbTrainingScaleHit(victim, who, damage); }
    HitMaterialType r = s_iHitOrig(cc, dir, damage, who);
    if (victim && who && s_wasLive && !wasDown && who->isPlayerCharacter()
        && (victim->isDead() || victim->isUnconcious() || victim->isDown()))
    {
        Faction* vf = victim->getFaction();
        if (vf && !wbIsPlayerFaction(vf))
        {
            Ogre::Vector3 p = victim->getPosition();
            EnterCriticalSection(&s_felledLock);
            if (s_felledN < 64) { s_felled[s_felledN].faction = vf; s_felled[s_felledN].x = p.x; s_felled[s_felledN].z = p.z; ++s_felledN; }
            LeaveCriticalSection(&s_felledLock);
        }
    }
    return r;
}
static void drainFelled()
{
    FelledEvent local[64]; int n = 0;
    EnterCriticalSection(&s_felledLock);
    n = s_felledN;
    for (int i = 0; i < n; ++i) local[i] = s_felled[i];
    s_felledN = 0;
    LeaveCriticalSection(&s_felledLock);
    for (int i = 0; i < n; ++i) wbPeopleOnFelled(local[i].faction, local[i].x, local[i].z);
}

// ===================================================================
// entry
// ===================================================================
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) s_module = inst;
    return TRUE;
}

__declspec(dllexport) void startPlugin()
{
    DebugLog("Warborn: World v0.1 - living world, wars, reputation, contracts, recruiting");
    srand((unsigned)GetTickCount());
    InitializeCriticalSection(&s_felledLock);
    memset(s_keyPrev, 0, sizeof(s_keyPrev));
    memset(s_keyNow, 0, sizeof(s_keyNow));

    char path[MAX_PATH] = "";
    if (s_module && GetModuleFileNameA(s_module, path, MAX_PATH))
    {
        char* slash = strrchr(path, '\\');
        if (slash) slash[1] = 0;
    }
    char folder[MAX_PATH];
    strcpy_s(folder, sizeof(folder), path);              // the mod folder, with a trailing slash
    strcat_s(path, sizeof(path), "Warborn.ini");
    wbLoadConfig(path);
    wbPersistInstall(folder);
    wbTrainingInit();
    wbFollowersInstall();
    wbCombatInstall(path);
    wbBattleInstall(path);

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff),
            (void*)&mainLoop_hook, &s_mainLoopOrig))
        ErrorLog("Warborn: mainLoop hook FAILED - mod inactive");
    else
        DebugLog("Warborn: mainLoop hook OK");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&GameWorld::processKeys),
            (void*)&processKeys_hook, &s_processKeysOrig))
        ErrorLog("Warborn: processKeys hook FAILED - Alt keys may also trigger Kenshi hotkeys");
    else
        DebugLog("Warborn: processKeys hook OK");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CameraClass::update),
            (void*)&cameraUpdate_hook, &s_cameraUpdateOrig))
        ErrorLog("Warborn: camera hook FAILED - no messages / journal");
    else
        DebugLog("Warborn: camera hook OK");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&CombatClass::_iHitYouAreYouHit),
            (void*)&iHit_hook, &s_iHitOrig))
        ErrorLog("Warborn: hit hook FAILED - reputation from fights off");
    else
        DebugLog("Warborn: hit hook OK");
}
