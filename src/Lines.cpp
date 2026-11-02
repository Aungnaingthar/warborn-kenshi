// Warborn: World - what messengers, criers and people say.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
// All lines are original. Placeholders: {A} attacker, {D} defender, {T} town,
// {H} home town, {R} reason, {N} a person's name, {F} a faction, {X} a title.

#include "Warborn.h"

#include <stdlib.h>
#include <string>

static const char* const RUMOUR[] = {
    "Rumour: the {A} are gathering warriors at {H}, eyeing {T} of the {D} {R}.",
    "Traders whisper that the {A} are sharpening blades in {H}. Their eyes are on {T} {R}.",
    "A scout swears the {A} are massing near {H}. {T} of the {D} could be next, {R}.",
    "Smoke from many cook-fires at {H}: the {A} are preparing to march on {T}, {R}.",
    "The {D} at {T} are nervous. Word is the {A} are coming {R}.",
    "Drifters on the road say the {A} have called their fighters to {H}. {T} is the talk, {R}.",
};
static const char* const WAR_START[] = {
    "WAR! The {A} march on {T}. The {D} must hold it.",
    "The {A} have left {H} in force. Their banners are heading for {T}!",
    "Alarm bells in {T}: the {A} are at the gates. The {D} call every blade to the walls.",
    "The {A} have begun their assault on {T}. The {D} are fighting for their lives.",
    "Blood on the road to {T}: the {A} attack the {D}.",
};
static const char* const TOWN_FELL[] = {
    "{T} has fallen! The {A} took it from the {D}.",
    "The {A} banner flies over {T}. The {D} were driven out.",
    "{T} belongs to the {A} now. The {D} will not forget this.",
    "The walls of {T} are broken. The {A} are its new masters.",
    "Survivors flee {T}: the {A} have won. The {D} swear revenge.",
};
static const char* const TOWN_HELD[] = {
    "The {D} held {T}. The {A} assault was driven off.",
    "{T} stands! The {A} retreat, leaving their dead in the sand.",
    "The {A} broke against the walls of {T}. The {D} are celebrating.",
    "The siege of {T} is over. The {D} kept their town, the {A} limp home.",
    "Against the odds, {T} did not fall. The {A} withdraw to {H}.",
};
static const char* const STRONGEST[] = {
    "The {A} now rule {X} towns - the mightiest power in the land.",
    "No one holds more towns than the {A} ({X}). Kings and clans alike fear them.",
    "With {X} towns under their banner, the {A} stand above all others.",
};
static const char* const TRUCE[] = {
    "Exhausted by war, the {A} and the {D} have agreed to a truce.",
    "Envoys met in the dust: the {A} and the {D} lay down their arms - for now.",
    "Too many graves. The {A} and the {D} have sworn a truce.",
};
static const char* const COALITION[] = {
    "Fearing the {T}, the {A} and the {D} have joined forces against them.",
    "Old enemies become friends: the {A} and the {D} will stand together against the {T}.",
    "A pact is sealed. The {A} and the {D} swear to break the power of the {T}.",
};
static const char* const RECRUIT_YES[] = {
    "{N}: \"Alright. I've got nothing better to live for. Lead the way.\"",
    "{N}: \"They say you pay in blood and glory. I'll take both.\"",
    "{N}: \"I've heard of you. I'd rather be on your side than in your way.\"",
    "{N}: \"Fine. But if you get me killed, I'm haunting you.\"",
    "{N}: \"Count me in. It beats starving out here.\"",
    "{N}: \"Your name carries weight. I'll follow it.\"",
};
static const char* const RECRUIT_NO[] = {
    "{N}: \"No. I don't know you well enough to die for you.\"",
    "{N}: \"Find someone else. My place is here.\"",
    "{N}: \"Prove yourself to the {F} first. Then we'll talk.\"",
    "{N}: \"Ha! Follow you? Come back when people know your name.\"",
    "{N}: \"Not today, stranger.\"",
    "{N}: \"I've seen your kind before. They never come back.\"",
};
static const char* const RECRUIT_HOSTILE[] = {
    "{N} spits at your feet. The {F} are your enemies.",
    "{N}: \"Join you? I'd sooner join the beak things.\" (The {F} are hostile.)",
    "{N} reaches for a weapon. The {F} want you dead.",
};
static const char* const NEW_TITLE[] = {
    "People have started calling you \"{X}\".",
    "Word of your deeds spreads. You are now known as \"{X}\".",
    "In taverns across the land they speak of \"{X}\".",
};

#define POOL(a) a, (int)(sizeof(a) / sizeof(a[0]))
struct PoolRef { const char* const* lines; int n; };

static std::string fill(const char* tpl, const WbVars& v)
{
    std::string out;
    for (const char* p = tpl; *p; ++p)
    {
        if (p[0] == '{' && p[1] && p[2] == '}')
        {
            const std::string* s = NULL;
            switch (p[1])
            {
                case 'A': s = &v.A; break; case 'D': s = &v.D; break; case 'T': s = &v.T; break;
                case 'H': s = &v.H; break; case 'R': s = &v.R; break; case 'N': s = &v.N; break;
                case 'F': s = &v.F; break; case 'X': s = &v.X; break;
            }
            if (s) { out += *s; p += 2; continue; }
        }
        out += *p;
    }
    return out;
}

std::string wbLine(int kind, const WbVars& v)
{
    PoolRef pools[LINE_COUNT] = {
        { POOL(RUMOUR) }, { POOL(WAR_START) }, { POOL(TOWN_FELL) }, { POOL(TOWN_HELD) },
        { POOL(STRONGEST) }, { POOL(TRUCE) }, { POOL(COALITION) }, { POOL(RECRUIT_YES) },
        { POOL(RECRUIT_NO) }, { POOL(RECRUIT_HOSTILE) }, { POOL(NEW_TITLE) },
    };
    if (kind < 0 || kind >= LINE_COUNT) return std::string();
    const PoolRef& p = pools[kind];
    int i = (int)(wbRand01() * (float)p.n);
    if (i >= p.n) i = p.n - 1;
    return fill(p.lines[i], v);
}
