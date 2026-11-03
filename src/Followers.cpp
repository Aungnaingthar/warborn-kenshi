// Warborn: World - the people who follow you.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// Every one of your people has:
//   TRAIT   Loyal, Steady, Greedy, Ambitious, Coward, Zealot or Hothead
//   BOND    how they feel about you (0-100): grows with time, glory and pay,
//           shrinks when they get beaten down
//   ORIGIN  the faction they came from. People of YOUR land (or a zealot of
//           their own) are loyal even when the bond is weak.
//   AGE     everyone grows older; old fighters weaken and may die of age
//   PARTNER M marries two of your people (they are steadier together)
// LOYALTY = bond + origin + trait (+ partner, - prisoner). Low loyalty means
// BETRAYAL: stealing money, deserting to their old faction, or (prisoners)
// escaping. How likely depends on the trait - Loyal people almost never
// turn, the Greedy and Ambitious often do.
//
// Also here: talking to anyone (H), the starting ORIGIN choice, and easier
// smithing (more experience for smithing skills).

#include "Warborn.h"

#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Damages.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/FactionWarMgr.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <core/Functions.h>
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <string>

extern bool g_wbTalkPressed;
extern bool g_wbMarryPressed;

// ------------------------------------------------------------ traits
enum { TR_LOYAL = 0, TR_STEADY, TR_GREEDY, TR_AMBITIOUS, TR_COWARD, TR_ZEALOT, TR_HOTHEAD, TR_COUNT };
static const char* const TRAIT_NAME[TR_COUNT] = { "Loyal", "Steady", "Greedy", "Ambitious", "Coward", "Zealot", "Hothead" };
static const float TRAIT_BETRAY[TR_COUNT]     = { 0.15f,  0.6f,     1.6f,     1.4f,        1.2f,     0.8f,     1.0f };
static const float TRAIT_LOYALTY[TR_COUNT]    = { 20.0f,  8.0f,     -8.0f,    -5.0f,       -3.0f,    0.0f,     -2.0f };

// ------------------------------------------------------------ origins (start options)
enum { OR_NONE = -1, OR_NOBLE = 0, OR_MERCHANT, OR_WARRIOR, OR_EXILE, OR_SLAVEBORN, OR_COUNT };
static const char* const ORIGIN_NAME[OR_COUNT] = { "Noble", "Merchant", "Warrior", "Exile", "Slave-born" };
static const char* const ORIGIN_DESC[OR_COUNT] = {
    "Born to a great house. +2000 cats, +20 renown, the two greatest powers think well of you, your people respect you (+bond).",
    "You know the roads and the deals. +5000 cats, every faction you know is friendlier, contracts pay 50% more.",
    "Raised with a blade in hand. All your people gain combat experience now, +10 renown.",
    "Cast out by the mightiest power. They dislike you, but outcasts flock to you (+15% recruiting), +5 renown.",
    "You know the chain and the whip. Your people trust you deeply (+loyalty, betrayal halved), but the world looks down on you.",
};
static int s_origin = OR_NONE;

// ------------------------------------------------------------ followers
struct Follower
{
    Character*  c;
    std::string name;
    int         trait;
    float       bond;
    float       age;
    std::string origin;        // faction name they came from ("" = your own land)
    bool        prisoner;
    std::string partner;       // name, "" = none
    bool        wasDown;
    double      nextBetrayCheck;
};
static std::vector<Follower> s_fol;
static std::vector<Follower> s_restore;      // loaded from the save, matched by name
static double s_lastTick = -1.0;
static int    s_lastYearLogged = 0;

static unsigned int hashName(const std::string& n)
{
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < n.size(); ++i) { h ^= (unsigned char)n[i]; h *= 16777619u; }
    return h;
}
static Follower* find(Character* c)
{
    for (size_t i = 0; i < s_fol.size(); ++i) if (s_fol[i].c == c) return &s_fol[i];
    return NULL;
}
static Follower* findByName(const std::string& n)
{
    for (size_t i = 0; i < s_fol.size(); ++i) if (s_fol[i].name == n) return &s_fol[i];
    return NULL;
}
static Follower makeFollower(Character* c, const std::string& origin, bool prisoner)
{
    Follower f;
    f.c = c; f.name = c->getName();
    unsigned int h = hashName(f.name);
    f.trait = (int)(h % TR_COUNT);
    f.bond = prisoner ? 5.0f : (origin.empty() ? 45.0f : 30.0f);
    if (s_origin == OR_NOBLE) f.bond += 10.0f;
    f.age = 18.0f + (float)((h >> 8) % 28);
    f.origin = origin; f.prisoner = prisoner; f.partner = "";
    f.wasDown = false;
    f.nextBetrayCheck = wbWorldMinutes() + 10.0;
    return f;
}

static float loyalty(const Follower& f)
{
    float l = f.bond + TRAIT_LOYALTY[f.trait];
    Faction* pf = wbPlayerFaction();
    if (f.origin.empty() || (pf && f.origin == wbFactionName(pf))) l += 30.0f;     // your own land
    else if (f.trait == TR_ZEALOT) l -= 10.0f;                                       // devoted to their old faction
    if (!f.partner.empty()) l += 10.0f;
    if (f.prisoner) l -= 30.0f;
    if (s_origin == OR_SLAVEBORN) l += 15.0f;
    return l;
}
static const char* loyaltyWord(float l)
{
    return l >= 80 ? "devoted" : l >= 55 ? "loyal" : l >= 40 ? "uncertain" : l >= 20 ? "restless" : "plotting";
}

float wbFollowerLoyalty(Character* c)
{
    Follower* f = find(c);
    return f ? loyalty(*f) : 50.0f;
}
void wbFollowersAllBond(float delta)
{
    for (size_t i = 0; i < s_fol.size(); ++i)
    {
        s_fol[i].bond += delta;
        if (s_fol[i].bond < 0.0f) s_fol[i].bond = 0.0f;
        if (s_fol[i].bond > 100.0f) s_fol[i].bond = 100.0f;
    }
}

// ------------------------------------------------------------ joining
void wbFollowerJoined(Character* c, Faction* origin, bool prisoner)
{
    if (!c) return;
    Follower* f = find(c);
    std::string o = (origin && !wbIsPlayerFaction(origin)) ? wbFactionName(origin) : std::string();
    if (f) { f->origin = o; f->prisoner = prisoner; if (prisoner) f->bond = 5.0f; return; }
    s_fol.push_back(makeFollower(c, o, prisoner));
    Follower& n = s_fol.back();
    char b[200];
    sprintf_s(b, sizeof(b), "%s is %s, about %d years old.", n.name.c_str(), TRAIT_NAME[n.trait], (int)n.age);
    wbToast(b);
}
float wbRecruitBonus() { return s_origin == OR_EXILE ? 0.15f : 0.0f; }
float wbContractPayMult() { return s_origin == OR_MERCHANT ? 1.5f : 1.0f; }

// glory is shared: renown raises your people's bond
void wbFollowersGlory(float renown)
{
    for (size_t i = 0; i < s_fol.size(); ++i)
    {
        float k = s_fol[i].trait == TR_AMBITIOUS ? 1.0f : 0.5f;
        s_fol[i].bond += renown * k;
        if (s_fol[i].bond > 100.0f) s_fol[i].bond = 100.0f;
    }
}
// money paid to the band (contracts) pleases the greedy
void wbFollowersPaid(int cats)
{
    for (size_t i = 0; i < s_fol.size(); ++i)
    {
        float k = s_fol[i].trait == TR_GREEDY ? 1.5f : 0.3f;
        s_fol[i].bond += k * (float)cats / 500.0f;
        if (s_fol[i].bond > 100.0f) s_fol[i].bond = 100.0f;
    }
}

// ------------------------------------------------------------ old age
static void killOfAge(Follower& f)
{
    MedicalSystem* m = f.c->getMedical();
    if (!m) return;
    Damages d(500.0f, 500.0f, 0.0f, 0.0f, 0.0f);
    for (uint32_t i = 0; i < m->anatomy.size(); ++i) if (m->anatomy[i]) m->anatomy[i]->applyDamage(d);
    char b[200];
    sprintf_s(b, sizeof(b), "%s has died of old age, at %d. The band mourns.", f.name.c_str(), (int)f.age);
    wbNews(b);
}
static void yearPassed(Follower& f)
{
    CharStats* st = f.c->getStats();
    if (st && f.age >= 55.0f)                                   // the body slows down
    {
        float& str = st->getStatRef(STAT_STRENGTH);
        float& dex = st->getStatRef(STAT_DEXTERITY);
        if (str > 5.0f) str -= 0.6f;
        if (dex > 5.0f) dex -= 0.6f;
    }
    if (f.age >= 70.0f && g_wb.oldAgeDeath && wbRand01() < 0.05f * (f.age - 69.0f)) killOfAge(f);
}

// ------------------------------------------------------------ betrayal
static void betray(Follower& f)
{
    Faction* home = f.origin.empty() ? NULL : wbFindFaction(f.origin);
    char b[260];
    if ((f.trait == TR_GREEDY || f.trait == TR_AMBITIOUS) && wbPlayerMoney() > 0)
    {
        int take = wbPlayerMoney() / 7; if (take > 1500) take = 1500; if (take < 1) take = 1;
        wbTakeMoneyByForce(take);
        sprintf_s(b, sizeof(b), "Betrayal! %s stole %d cats from the band's purse.", f.name.c_str(), take);
        wbNews(b);
        f.bond -= 10.0f;
        if (!home || !g_wb.betrayalDesert) return;              // stays, disgraced
    }
    if (home && g_wb.betrayalDesert)
    {
        sprintf_s(b, f.prisoner ? "%s escaped and ran back to the %s!" : "Betrayal! %s has deserted you and gone back to the %s.",
                  f.name.c_str(), f.origin.c_str());
        wbNews(b);
        wbLogAlways("[Warborn] betrayal: desertion");
        f.c->setFaction(home, NULL);
        // they are no longer ours: the follower record goes on the next sync
        return;
    }
    // nowhere to go: they sulk and refuse to help for a while
    sprintf_s(b, sizeof(b), "%s grumbles openly about you. Their loyalty is failing.", f.name.c_str());
    wbToast(b);
    f.bond -= 5.0f;
}

// ------------------------------------------------------------ marriage
static void marry()
{
    Character* a = ou->player->selectedCharacter.getCharacter();
    if (!a || !a->isPlayerCharacter()) { wbToast("Select one of your people, then press Alt+M."); return; }
    Follower* fa = find(a);
    if (!fa) return;
    if (!fa->partner.empty()) { wbToast((fa->name + " is already married to " + fa->partner + ".").c_str()); return; }
    Follower* best = NULL; float bestD2 = 150.0f * 150.0f;
    Ogre::Vector3 p = a->getPosition();
    for (size_t i = 0; i < s_fol.size(); ++i)
    {
        Follower& o = s_fol[i];
        if (o.c == a || !wbIsOwned(o.c) || !o.partner.empty() || o.prisoner || o.c->isDead()) continue;
        Ogre::Vector3 q = o.c->getPosition();
        float d2 = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z);
        if (d2 < bestD2) { bestD2 = d2; best = &o; }
    }
    if (!best) { wbToast("Nobody suitable nearby (another of your people, unmarried, within ~15 m)."); return; }
    float chance = 0.2f + (fa->bond + best->bond) / 200.0f;
    if (fa->prisoner) chance = 0.0f;
    if (wbRand01() < chance)
    {
        fa->partner = best->name; best->partner = fa->name;
        fa->bond += 10.0f; best->bond += 10.0f;
        wbNews((fa->name + " and " + best->name + " are now married. The band celebrates around the fire.").c_str());
        wbAddRenown(0.5f, "a wedding");
    }
    else wbToast((fa->name + " and " + best->name + " aren't ready for that. (more time together, a stronger bond)").c_str());
}

// ------------------------------------------------------------ talking
static Character* s_talkWith = NULL;
static int        s_talkTopic = 0;

static const char* const GREET_HOSTILE[]  = { "\"Walk away while you still can.\"", "\"Your kind isn't welcome here.\"", "\"One more step and you'll bleed.\"" };
static const char* const GREET_COLD[]     = { "\"What do you want?\"", "\"Make it quick.\"", "\"I've heard about you. Nothing good.\"" };
static const char* const GREET_NEUTRAL[]  = { "\"Hm. A traveller.\"", "\"Watch the road. It eats people.\"", "\"Got water? No? Then keep moving.\"", "\"Another day in the dust.\"" };
static const char* const GREET_WARM[]     = { "\"Ah, a friend of ours. Welcome.\"", "\"Good to see you. Stay a while.\"", "\"Our people speak well of you.\"" };
static const char* const GREET_ALLIED[]   = { "\"Brother in arms! What do you need?\"", "\"Anything for you. You've bled for us.\"", "\"The whole town owes you.\"" };
static const char* const OPINION[]        = {
    "\"You? Never heard of you.\"",
    "\"Some drifter with a sword. Seen plenty.\"",
    "\"You're the {X}, right? People talk.\"",
    "\"The {X}! I'd buy you a drink if I had the cats.\"",
    "\"The {X}... I didn't think you were real.\"",
};
static const char* const FOL_FEEL[]       = {
    "\"I'd walk into fire for you. You know that.\"",
    "\"I'm with you. Don't make me regret it.\"",
    "\"I'm still here. For now.\"",
    "\"Some days I wonder why I follow you.\"",
    "\"...\" (they avoid your eyes)",
};
static const char* const TRAIT_SAYS[TR_COUNT] = {
    "\"Where you go, I go.\"",
    "\"Just point me at the work.\"",
    "\"Pay is late again. You noticed?\"",
    "\"One day they'll sing about us. About ME.\"",
    "\"That last fight... I don't want to talk about it.\"",
    "\"My people taught me what matters. Remember that.\"",
    "\"When do we fight? I'm bored.\"",
};

#define PICK(a) a[(int)(wbRand01() * (float)(sizeof(a) / sizeof(a[0]))) % (int)(sizeof(a) / sizeof(a[0]))]

static std::string fillX(const char* s)
{
    std::string o = s; size_t p = o.find("{X}");
    if (p != std::string::npos) o.replace(p, 3, wbRenownTitle());
    return o;
}

static void talk()
{
    Character* c = wbSelectedPerson();
    if (!c) { wbToast("Point at someone with the mouse, then press Alt+H."); return; }
    if (c->isDead()) { wbToast("The dead keep their secrets."); return; }
    if (c->isUnconcious()) { wbToast("They're out cold."); return; }
    if (c != s_talkWith) { s_talkWith = c; s_talkTopic = 0; }
    std::string name = c->getName();
    std::string line;

    if (c->isPlayerCharacter())
    {
        Follower* f = find(c);
        if (!f) { wbToast((name + ": \"Yes?\"").c_str()); return; }
        float l = loyalty(*f);
        switch (s_talkTopic % 3)
        {
            case 0: { int i = l >= 80 ? 0 : l >= 55 ? 1 : l >= 40 ? 2 : l >= 20 ? 3 : 4; line = FOL_FEEL[i]; break; }
            case 1: line = TRAIT_SAYS[f->trait]; break;
            default:
            {
                char b[220];
                sprintf_s(b, sizeof(b), "(%s, age %d, %s%s%s. Bond %d, %s.)", TRAIT_NAME[f->trait], (int)f->age,
                          f->origin.empty() ? "of your own land" : ("from the " + f->origin).c_str(),
                          f->partner.empty() ? "" : ", married to ", f->partner.c_str(), (int)f->bond, loyaltyWord(l));
                line = b;
                break;
            }
        }
    }
    else
    {
        Faction* pf = wbPlayerFaction();
        Faction* vf = c->getFaction();
        float rel = (vf && vf->relations && pf) ? vf->relations->getFactionRelation(pf) : 0.0f;
        bool hostile = vf && vf->relations && pf && vf->relations->isEnemy(pf);
        switch (s_talkTopic % 4)
        {
            case 0:
                line = hostile ? PICK(GREET_HOSTILE) : rel < -15 ? PICK(GREET_COLD) : rel < 15 ? PICK(GREET_NEUTRAL)
                     : rel < 50 ? PICK(GREET_WARM) : PICK(GREET_ALLIED);
                break;
            case 1:
            {
                std::string r = wbLatestNews();
                line = r.empty() ? "\"Quiet lately. Too quiet.\"" : "\"Heard the news? " + r + "\"";
                break;
            }
            case 2:
            {
                float fame = wbRenown();
                int i = fame < 8 ? 0 : fame < 20 ? 1 : fame < 70 ? 2 : fame < 170 ? 3 : 4;
                line = fillX(OPINION[i]);
                break;
            }
            default:
                if (hostile) line = "\"We have nothing to say to each other.\"";
                else if (wbContractsOfferCount() > 0) line = "\"There's work, if you want it. Check your journal (Alt+J).\"";
                else line = "\"Nothing for you today. Come back later.\"";
                if (!hostile && rel >= 15) line += " (They might join you: Alt+R.)";
                break;
        }
    }
    ++s_talkTopic;
    wbToast((name + ": " + line).c_str());
}

// ------------------------------------------------------------ origins
void wbFollowersChooseOrigin(int which)
{
    if (s_origin != OR_NONE || which < 0 || which >= OR_COUNT) return;
    Faction* pf = wbPlayerFaction();
    if (!pf || !ou || !ou->factionMgr) return;
    s_origin = which;
    const lektor<Faction*>* all = ou->factionMgr->getAllFactions();
    // the two greatest powers
    Faction* top1 = NULL; Faction* top2 = NULL; int t1 = 0, t2 = 0;
    for (uint32_t i = 0; all && i < all->size(); ++i)
    {
        Faction* f = (*all)[i];
        if (!f || f == pf) continue;
        int n = wbTownCount(f);
        if (n > t1) { top2 = top1; t2 = t1; top1 = f; t1 = n; }
        else if (n > t2) { top2 = f; t2 = n; }
    }
    switch (which)
    {
        case OR_NOBLE:
            wbPayPlayer(2000);
            wbAddRenown(20.0f, "noble birth");
            if (top1) wbGiveRep(top1, 15.0f, "for your noble name");
            if (top2) wbGiveRep(top2, 15.0f, "for your noble name");
            for (size_t i = 0; i < s_fol.size(); ++i) s_fol[i].bond += 10.0f;
            break;
        case OR_MERCHANT:
            wbPayPlayer(5000);
            for (uint32_t i = 0; all && i < all->size(); ++i)
            {
                Faction* f = (*all)[i];
                if (f && f != pf && ou->player->isFactionKnown(f) && f->relations && !f->relations->isEnemy(pf))
                    wbGiveRep(f, 5.0f, "for your trade");
            }
            break;
        case OR_WARRIOR:
        {
            const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
            for (uint32_t i = 0; i < mine.size(); ++i)
            {
                CharStats* st = mine[i] ? mine[i]->getStats() : NULL;
                if (!st) continue;
                st->xpStat_eventBased(STAT_MELEE_ATTACK, 6.0f);
                st->xpStat_eventBased(STAT_MELEE_DEFENCE, 6.0f);
                st->xpStat_eventBased(STAT_DODGE, 3.0f);
                st->xpStat_eventBased(STAT_TOUGHNESS, 3.0f);
            }
            wbAddRenown(10.0f, "a warrior's upbringing");
            break;
        }
        case OR_EXILE:
            if (top1) wbGiveRep(top1, -20.0f, "for the exile they cast out");
            wbAddRenown(5.0f, "a famous exile");
            break;
        case OR_SLAVEBORN:
            for (uint32_t i = 0; all && i < all->size(); ++i)
            {
                Faction* f = (*all)[i];
                if (f && f != pf && ou->player->isFactionKnown(f)) wbGiveRep(f, -4.0f, "for your low birth");
            }
            break;
    }
    wbNews(("Your story begins: you are " + std::string(ORIGIN_NAME[which]) + ".").c_str());
}
std::string wbOriginSummary()
{
    std::string out;
    if (s_origin != OR_NONE)
    {
        out = "YOUR ORIGIN: " + std::string(ORIGIN_NAME[s_origin]) + "\n  " + ORIGIN_DESC[s_origin] + "\n";
        return out;
    }
    out = "CHOOSE YOUR ORIGIN (once per game) - press a button below\n\n";
    for (int i = 0; i < OR_COUNT; ++i) out += "  " + std::string(ORIGIN_NAME[i]) + ": " + ORIGIN_DESC[i] + "\n\n";
    return out;
}
bool wbOriginChosen() { return s_origin != OR_NONE; }
const char* wbOriginName(int i) { return (i >= 0 && i < OR_COUNT) ? ORIGIN_NAME[i] : ""; }

// ------------------------------------------------------------ the band summary (journal)
std::string wbFollowersSummary()
{
    std::string out = "YOUR PEOPLE\n";
    char b[260];
    for (size_t i = 0; i < s_fol.size() && i < 24; ++i)
    {
        const Follower& f = s_fol[i];
        float l = loyalty(f);
        sprintf_s(b, sizeof(b), "  %s - %s, age %d%s%s, bond %d, %s%s\n", f.name.c_str(), TRAIT_NAME[f.trait], (int)f.age,
                  f.partner.empty() ? "" : ", married to ", f.partner.c_str(), (int)f.bond, loyaltyWord(l),
                  f.prisoner ? " (PRISONER)" : "");
        out += b;
    }
    if (s_fol.empty()) out += "  (nobody yet)\n";
    out += "  Alt+H talk to the person under the cursor, Alt+M marry two of your people, Alt+R recruit (Shift+Alt+R: into the warband)\n";
    return out;
}

// ------------------------------------------------------------ frame
void wbFollowersFrame()
{
    if (!ou || !ou->player) return;
    if (g_wbTalkPressed) talk();
    if (g_wbMarryPressed) marry();
    double now = wbWorldMinutes();
    if (s_lastTick < 0.0) s_lastTick = now;
    double dt = now - s_lastTick;
    if (dt < 0.1) return;                                       // every ~6 s
    s_lastTick = now;

    // sync with the people we actually have
    const lektor<Character*>& mine = ou->player->getAllPlayerCharacters();
    for (uint32_t i = 0; i < mine.size(); ++i)
    {
        Character* c = mine[i];
        if (!c || find(c)) continue;
        std::string n = c->getName();
        bool restored = false;
        for (size_t k = 0; k < s_restore.size(); ++k)
            if (s_restore[k].name == n) { Follower f = s_restore[k]; f.c = c; f.wasDown = false; f.nextBetrayCheck = now + 10.0;
                                          s_fol.push_back(f); s_restore.erase(s_restore.begin() + k); restored = true; break; }
        if (!restored) s_fol.push_back(makeFollower(c, std::string(), false));   // was already yours: your own land
    }
    for (size_t i = 0; i < s_fol.size(); )
    {
        Follower& f = s_fol[i];
        bool still = false;
        for (uint32_t k = 0; k < mine.size(); ++k) if (mine[k] == f.c) { still = true; break; }
        if (!still || f.c->isDead())
        {
            if (!f.partner.empty())
            {
                Follower* p = findByName(f.partner);
                if (p && p->c != f.c) { p->partner = ""; p->bond -= 15.0f;
                    if (f.c->isDead() || !still) wbNews((p->name + " grieves for " + f.name + ".").c_str()); }
            }
            s_fol.erase(s_fol.begin() + i);
            continue;
        }
        // time together
        f.bond += (float)dt * (f.prisoner ? 0.5f : 0.2f);
        if (f.prisoner && f.bond >= 45.0f)
        {
            f.prisoner = false;
            wbNews((f.name + " is no longer a prisoner. They have chosen to stay with you.").c_str());
        }
        // beaten down in a fight
        bool down = f.c->isUnconcious() || f.c->isDown();
        if (down && !f.wasDown) f.bond -= (f.trait == TR_COWARD) ? 5.0f : 1.5f;
        f.wasDown = down;
        if (f.bond > 100.0f) f.bond = 100.0f;
        if (f.bond < 0.0f) f.bond = 0.0f;
        // ageing
        int yearBefore = (int)f.age;
        f.age += (float)dt / g_wb.yearMinutes;
        if ((int)f.age > yearBefore) yearPassed(f);
        // loyalty and betrayal
        if (g_wb.betrayal && now >= f.nextBetrayCheck)
        {
            f.nextBetrayCheck = now + 10.0;
            float l = loyalty(f);
            if (l < 40.0f)
            {
                float chance = (40.0f - l) / 100.0f * TRAIT_BETRAY[f.trait] * g_wb.betrayalRate;
                if (s_origin == OR_SLAVEBORN) chance *= 0.5f;
                char lb[160]; sprintf_s(lb, sizeof(lb), "[Warborn] loyalty %s %.0f -> betrayal chance %.2f", f.name.c_str(), l, chance);
                wbLog(lb);
                if (wbRand01() < chance) betray(f);
                else if (l < 25.0f) wbToast((f.name + " seems restless. (low loyalty - talk to them, pay them, win glory)").c_str());
            }
        }
        ++i;
    }
}

// ------------------------------------------------------------ easier smithing / learning
static void (*s_xpOrig)(CharStats*, StatsEnumerated, float) = NULL;
static void xp_hook(CharStats* st, StatsEnumerated stat, float amount)
{
    if (stat == STAT_SMITHING_WEAPON || stat == STAT_SMITHING_ARMOUR || stat == STAT_SMITHING_BOW)
        amount *= g_wb.smithingXpMult;
    amount *= g_wb.learnMult;
    s_xpOrig(st, stat, amount);
}

void wbFollowersInstall()
{
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(&CharStats::xpStat_eventBased), (void*)&xp_hook, &s_xpOrig))
        ErrorLog("Warborn: experience hook FAILED - easier smithing off");
    else
        DebugLog("Warborn: experience hook OK (easier smithing)");
}

// ------------------------------------------------------------ saving
void wbFollowersSave(std::vector<std::string>& out)
{
    char b[96];
    sprintf_s(b, sizeof(b), "origin\t%d", s_origin);
    out.push_back(b);
    for (size_t i = 0; i < s_fol.size(); ++i)
    {
        const Follower& f = s_fol[i];
        sprintf_s(b, sizeof(b), "%d\t%.2f\t%.3f\t%d", f.trait, f.bond, f.age, f.prisoner ? 1 : 0);
        out.push_back("fol\t" + wbClean(f.name) + "\t" + b + "\t" + (f.origin.empty() ? "-" : wbClean(f.origin))
                      + "\t" + (f.partner.empty() ? "-" : wbClean(f.partner)));
    }
}
void wbFollowersLoad(const std::vector<std::string>& in)
{
    for (size_t i = 0; i < in.size(); ++i)
    {
        std::vector<std::string> f = wbSplit(in[i]);
        if (f[0] == "origin" && f.size() >= 2) s_origin = atoi(f[1].c_str());
        else if (f[0] == "fol" && f.size() >= 8)
        {
            Follower x;
            x.c = NULL; x.name = f[1]; x.trait = atoi(f[2].c_str()) % TR_COUNT; if (x.trait < 0) x.trait = 0;
            x.bond = (float)atof(f[3].c_str()); x.age = (float)atof(f[4].c_str()); x.prisoner = f[5] == "1";
            x.origin = f[6] == "-" ? std::string() : f[6]; x.partner = f[7] == "-" ? std::string() : f[7];
            x.wasDown = false; x.nextBetrayCheck = 0.0;
            s_restore.push_back(x);
        }
    }
    if (s_origin < OR_NONE || s_origin >= OR_COUNT) s_origin = OR_NONE;
}
void wbFollowersReset()
{
    s_fol.clear(); s_restore.clear(); s_origin = OR_NONE; s_lastTick = -1.0; s_talkWith = NULL;
}
