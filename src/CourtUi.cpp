// Warborn: World - the Court Hall window (Alt+C).
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
//
// A visual court: your banner at the top, lines down to six office cards
// (each office has its own colour, the holder's name, skill and a loyalty
// bar), a treasury & stores panel and a list of candidates on the right.
// Click a candidate, then an office card, to appoint; click a card, then
// Dismiss, to remove. Army - / + / March! command the General's army.
//
// Threads: the main loop builds a snapshot (wbCourtView) and carries out
// requests; this UI side only draws the snapshot and posts requests.

#include "Warborn.h"

#include <Debug.h>
#include <mygui/MyGUI.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <vector>
#include <string>

// ------------------------------------------------------------ shared with the main loop
static CRITICAL_SECTION   s_lock;
static bool               s_lockReady = false;
static WbCourtViewData    s_view;               // locked
static bool               s_viewDirty = false;  // locked
static volatile bool      s_open      = false;
static volatile bool      s_rebuild   = true;
static volatile int       s_reqAppointOffice = -1;
static Character* volatile s_reqAppointWho  = NULL;
static volatile int       s_reqDismiss = -1;
static volatile int       s_reqArmy    = -2;    // -1, +1, -2 = none
static volatile bool      s_reqCampaign = false;
static std::string        s_reqCampaignTown;    // locked
static ULONGLONG          s_nextBuild  = 0;

static void lockInit() { if (!s_lockReady) { InitializeCriticalSection(&s_lock); s_lockReady = true; } }

void wbCourtUiMainFrame()
{
    lockInit();
    int o = s_reqAppointOffice;
    if (o >= 0) { Character* who = s_reqAppointWho; s_reqAppointOffice = -1; s_reqAppointWho = NULL; wbCourtAppoint(o, who); s_rebuild = true; }
    int d = s_reqDismiss;
    if (d >= 0) { s_reqDismiss = -1; wbCourtDismiss(d); s_rebuild = true; }
    int a = s_reqArmy;
    if (a != -2) { s_reqArmy = -2; wbCourtArmy(a); s_rebuild = true; }
    if (s_reqCampaign)
    {
        std::string town;
        EnterCriticalSection(&s_lock); town = s_reqCampaignTown; s_reqCampaign = false; LeaveCriticalSection(&s_lock);
        wbCourtCampaign(town);
        s_rebuild = true;
    }
    if (!s_open) return;
    ULONGLONG now = GetTickCount64();
    if (!s_rebuild && now < s_nextBuild) return;
    s_rebuild = false; s_nextBuild = now + 1000;
    WbCourtViewData v;
    wbCourtView(v);
    EnterCriticalSection(&s_lock);
    s_view = v; s_viewDirty = true;
    LeaveCriticalSection(&s_lock);
}

// ------------------------------------------------------------ widgets
static const int  OFFICES = 6;
static const int  PAGE    = 9;
static const MyGUI::Colour OFFICE_COL[OFFICES] = {
    MyGUI::Colour(0.86f, 0.68f, 0.25f, 1.0f),    // Finance Minister - gold
    MyGUI::Colour(0.36f, 0.66f, 0.30f, 1.0f),    // Steward - green
    MyGUI::Colour(0.78f, 0.26f, 0.20f, 1.0f),    // General - red
    MyGUI::Colour(0.52f, 0.32f, 0.68f, 1.0f),    // Spymaster - purple
    MyGUI::Colour(0.26f, 0.52f, 0.82f, 1.0f),    // Envoy - blue
    MyGUI::Colour(0.88f, 0.48f, 0.16f, 1.0f),    // Drillmaster - orange
};

struct Card
{
    MyGUI::Button*  base;
    MyGUI::Widget*  header;
    MyGUI::TextBox* title;
    MyGUI::TextBox* name;
    MyGUI::TextBox* line;
    MyGUI::Widget*  barBack;
    MyGUI::Widget*  barFill;
    MyGUI::Widget*  sel[4];       // highlight frame
    int barW;
};

static MyGUI::Window*  s_win = NULL;
static bool            s_tried = false;
static Card            s_cards[OFFICES];
static MyGUI::TextBox* s_leader = NULL;
static MyGUI::TextBox* s_leaderLine = NULL;
static MyGUI::TextBox* s_money = NULL;
static MyGUI::TextBox* s_food = NULL;
static MyGUI::Widget*  s_foodFill = NULL;
static int             s_foodBarW = 0;
static MyGUI::TextBox* s_tax = NULL;
static MyGUI::TextBox* s_army = NULL;
static MyGUI::Button*  s_candBtn[PAGE];
static MyGUI::TextBox* s_candTitle = NULL;
static MyGUI::Button*  s_btnPrev = NULL;
static MyGUI::Button*  s_btnNext = NULL;
static MyGUI::Button*  s_btnDismiss = NULL;
static MyGUI::Button*  s_btnArmyMinus = NULL;
static MyGUI::Button*  s_btnArmyPlus = NULL;
static MyGUI::Button*  s_btnMarch = NULL;           // "Order campaign"
static MyGUI::Button*  s_btnTgtPrev = NULL;
static MyGUI::Button*  s_btnTgtNext = NULL;
static MyGUI::TextBox* s_target = NULL;
static MyGUI::TextBox* s_people = NULL;
static MyGUI::TextBox* s_campaign = NULL;
static int             s_targetIdx = 0;
static MyGUI::Button*  s_btnClose = NULL;
static MyGUI::TextBox* s_hint = NULL;

static WbCourtViewData s_shown;                 // UI-thread copy of the last snapshot
static int             s_page = 0;
static int             s_selCand = -1;          // index into s_shown.cands
static int             s_selOffice = -1;

static MyGUI::Widget* rect(MyGUI::Widget* parent, int x, int y, int w, int h, const MyGUI::Colour& c)
{
    MyGUI::Widget* r = parent->createWidget<MyGUI::Widget>("WhiteSkin", x, y, w, h, MyGUI::Align::Default, "");
    r->setColour(c);
    r->setNeedMouseFocus(false);
    return r;
}
static MyGUI::TextBox* text(MyGUI::Widget* parent, int x, int y, int w, int h, const char* skin,
                            const MyGUI::Colour& c, MyGUI::Align a)
{
    MyGUI::TextBox* t = parent->createWidget<MyGUI::TextBox>(skin, x, y, w, h, MyGUI::Align::Default, "");
    t->setTextColour(c);
    t->setTextAlign(a);
    t->setNeedMouseFocus(false);
    return t;
}
static MyGUI::Button* button(MyGUI::Widget* parent, int x, int y, int w, int h, const char* caption)
{
    MyGUI::Button* b = parent->createWidget<MyGUI::Button>("Kenshi_Button1", x, y, w, h, MyGUI::Align::Default, "");
    b->setCaption(caption);
    return b;
}

static void onCard(MyGUI::Widget* sender)
{
    for (int o = 0; o < OFFICES; ++o)
        if (sender == s_cards[o].base)
        {
            if (s_selCand >= 0 && s_selCand < (int)s_shown.cands.size())
            {
                s_reqAppointWho = s_shown.cands[s_selCand].who;
                s_reqAppointOffice = o;
                s_selCand = -1; s_selOffice = -1;
            }
            else s_selOffice = (s_selOffice == o) ? -1 : o;
            s_rebuild = true;
        }
}
static void onCand(MyGUI::Widget* sender)
{
    for (int i = 0; i < PAGE; ++i)
        if (sender == s_candBtn[i])
        {
            int idx = s_page * PAGE + i;
            s_selCand = (s_selCand == idx) ? -1 : idx;
            s_selOffice = -1;
            s_rebuild = true;
        }
}
static void onButton(MyGUI::Widget* sender)
{
    if (sender == s_btnPrev && s_page > 0) --s_page;
    else if (sender == s_btnNext && (s_page + 1) * PAGE < (int)s_shown.cands.size()) ++s_page;
    else if (sender == s_btnDismiss && s_selOffice >= 0) { s_reqDismiss = s_selOffice; s_selOffice = -1; }
    else if (sender == s_btnArmyMinus) s_reqArmy = -1;
    else if (sender == s_btnArmyPlus)  s_reqArmy = 1;
    else if (sender == s_btnTgtPrev && s_targetIdx > 0) --s_targetIdx;
    else if (sender == s_btnTgtNext && s_targetIdx + 1 < (int)s_shown.targets.size()) ++s_targetIdx;
    else if (sender == s_btnMarch && s_targetIdx < (int)s_shown.targets.size())
    {
        EnterCriticalSection(&s_lock); s_reqCampaignTown = s_shown.targets[s_targetIdx]; s_reqCampaign = true; LeaveCriticalSection(&s_lock);
    }
    else if (sender == s_btnClose && s_win) { s_win->setVisible(false); s_open = false; }
    s_rebuild = true;
}

static void build()
{
    if (s_win || s_tried) return;
    s_tried = true;
    try
    {
        MyGUI::Gui* g = MyGUI::Gui::getInstancePtr();
        if (!g) return;
        s_win = g->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", 0.07f, 0.05f, 0.86f, 0.88f,
                                                   MyGUI::Align::Center, "Window", "wb_court_hall");
        if (!s_win) return;
        s_win->setCaption("Court Hall");
        MyGUI::Widget* cl = s_win->getClientWidget();
        const int W = cl->getWidth(), H = cl->getHeight();
        const int leftW = (int)(W * 0.70f);
        const MyGUI::Colour gold(0.95f, 0.82f, 0.50f, 1.0f), pale(0.86f, 0.82f, 0.74f, 1.0f), dim(0.62f, 0.58f, 0.50f, 1.0f);
        const MyGUI::Colour line(0.55f, 0.46f, 0.30f, 1.0f);

        rect(cl, 0, 0, W, H, MyGUI::Colour(0.11f, 0.10f, 0.08f, 1.0f));                 // the hall
        rect(cl, leftW + 6, 0, 2, H, line);                                              // divider

        // --- your banner
        const int bw = (int)(leftW * 0.42f), bh = (int)(H * 0.13f), bx = (leftW - bw) / 2, by = 10;
        rect(cl, bx - 3, by - 3, bw + 6, bh + 6, line);
        rect(cl, bx, by, bw, bh, MyGUI::Colour(0.24f, 0.19f, 0.11f, 1.0f));
        rect(cl, bx, by, bw, 5, gold);
        s_leader     = text(cl, bx, by + 8, bw, 26, "Kenshi_TextboxStandardText", gold, MyGUI::Align::Center);
        s_leaderLine = text(cl, bx, by + 34, bw, 18, "Kenshi_TextboxStandardText_Small", pale, MyGUI::Align::Center);
        s_money      = text(cl, bx, by + 54, bw, 18, "Kenshi_TextboxStandardText_Small", gold, MyGUI::Align::Center);
        s_people     = text(cl, 0, by + bh + 6, leftW, 18, "Kenshi_TextboxStandardText_Small", pale, MyGUI::Align::Center);

        // --- office cards, two rows of three, with lines down from the banner
        const int cw = (int)(leftW * 0.27f), ch = (int)(H * 0.24f);
        const int rowY[2] = { (int)(H * 0.24f), (int)(H * 0.55f) };
        const int gap = (leftW - 3 * cw) / 4;
        const int midX = leftW / 2;
        rect(cl, midX - 1, by + bh + 26, 3, rowY[0] - (by + bh + 26) - 14, line);       // banner down (below the counts line)
        for (int r = 0; r < 2; ++r)
        {
            int left = gap + cw / 2, right = gap + 2 * (cw + gap) + cw / 2;
            rect(cl, left, rowY[r] - 14, right - left, 3, line);                         // the row line
            if (r == 1) rect(cl, midX - 1, rowY[0] + ch, 3, rowY[1] - rowY[0] - ch - 14, line);
            for (int k = 0; k < 3; ++k)
            {
                int o = r * 3 + k;
                int x = gap + k * (cw + gap), y = rowY[r];
                rect(cl, x + cw / 2 - 1, y - 14, 3, 14, line);                           // stub to the card
                Card& c = s_cards[o];
                c.base = cl->createWidget<MyGUI::Button>("Kenshi_Button1", x, y, cw, ch, MyGUI::Align::Default, "");
                c.base->eventMouseButtonClick += MyGUI::newDelegate(onCard);
                c.header = rect(c.base, 0, 0, cw, 26, OFFICE_COL[o]);
                c.title  = text(c.base, 0, 3, cw, 20, "Kenshi_TextboxStandardText_Small", MyGUI::Colour(0.08f, 0.06f, 0.04f, 1.0f), MyGUI::Align::Center);
                c.name   = text(c.base, 6, 36, cw - 12, 26, "Kenshi_TextboxStandardText", pale, MyGUI::Align::Center);
                c.line   = text(c.base, 6, 64, cw - 12, 18, "Kenshi_TextboxStandardText_Small", dim, MyGUI::Align::Center);
                c.barW   = cw - 30;
                text(c.base, 15, ch - 40, cw - 30, 16, "Kenshi_TextboxStandardText_Small", dim, MyGUI::Align::Left)->setCaption("loyalty");
                c.barBack = rect(c.base, 15, ch - 22, c.barW, 10, MyGUI::Colour(0.20f, 0.18f, 0.15f, 1.0f));
                c.barFill = rect(c.base, 15, ch - 22, 1, 10, MyGUI::Colour(0.4f, 0.7f, 0.3f, 1.0f));
                c.sel[0] = rect(cl, x - 4, y - 4, cw + 8, 3, gold);  c.sel[1] = rect(cl, x - 4, y + ch + 1, cw + 8, 3, gold);
                c.sel[2] = rect(cl, x - 4, y - 4, 3, ch + 8, gold);  c.sel[3] = rect(cl, x + cw + 1, y - 4, 3, ch + 8, gold);
                for (int s = 0; s < 4; ++s) c.sel[s]->setVisible(false);
            }
        }

        // --- the General's campaign: target, army size, order; and Dismiss
        const int ay = (int)(H * 0.86f), abh = 34;
        int x = gap;
        s_btnDismiss   = button(cl, x, ay, 100, abh, "Dismiss");                 x += 110;
        s_btnTgtPrev   = button(cl, x, ay, 34, abh, "<");                        x += 38;
        s_target       = text(cl, x, ay + 8, 220, 20, "Kenshi_TextboxStandardText_Small", gold, MyGUI::Align::Center); x += 224;
        s_btnTgtNext   = button(cl, x, ay, 34, abh, ">");                        x += 46;
        s_btnArmyMinus = button(cl, x, ay, 34, abh, "-");                        x += 38;
        s_army         = text(cl, x, ay + 8, 120, 20, "Kenshi_TextboxStandardText_Small", pale, MyGUI::Align::Center); x += 124;
        s_btnArmyPlus  = button(cl, x, ay, 34, abh, "+");                        x += 46;
        s_btnMarch     = button(cl, x, ay, leftW - gap - x > 120 ? leftW - gap - x : 120, abh, "Order campaign");
        s_campaign     = text(cl, gap, ay - 50, leftW - 2 * gap, 18, "Kenshi_TextboxStandardText_Small", gold, MyGUI::Align::Center);
        s_hint         = text(cl, gap, ay - 26, leftW - 2 * gap, 18, "Kenshi_TextboxStandardText_Small", dim, MyGUI::Align::Center);

        // --- right panel: treasury & stores, candidates
        const int px = leftW + 20, pw = W - px - 14;
        text(cl, px, 8, pw, 22, "Kenshi_TextboxStandardText", gold, MyGUI::Align::Left)->setCaption("TREASURY & STORES");
        rect(cl, px, 32, pw, 2, line);
        s_food = text(cl, px, 42, pw, 18, "Kenshi_TextboxStandardText_Small", pale, MyGUI::Align::Left);
        s_foodBarW = pw;
        rect(cl, px, 62, pw, 10, MyGUI::Colour(0.20f, 0.18f, 0.15f, 1.0f));
        s_foodFill = rect(cl, px, 62, 1, 10, MyGUI::Colour(0.45f, 0.70f, 0.30f, 1.0f));
        s_tax  = text(cl, px, 78, pw, 18, "Kenshi_TextboxStandardText_Small", pale, MyGUI::Align::Left);
        s_candTitle = text(cl, px, 112, pw, 22, "Kenshi_TextboxStandardText", gold, MyGUI::Align::Left);
        rect(cl, px, 136, pw, 2, line);
        const int rowH = (int)((H * 0.80f - 150) / PAGE);
        for (int i = 0; i < PAGE; ++i)
        {
            s_candBtn[i] = button(cl, px, 144 + i * rowH, pw, rowH - 4, "");
            s_candBtn[i]->eventMouseButtonClick += MyGUI::newDelegate(onCand);
        }
        s_btnPrev = button(cl, px, (int)(H * 0.86f), pw / 2 - 4, abh, "< Prev");
        s_btnNext = button(cl, px + pw / 2 + 4, (int)(H * 0.86f), pw / 2 - 4, abh, "Next >");
        s_btnClose = button(cl, W - 90, H - 36, 80, 30, "Close");

        MyGUI::Button* all[] = { s_btnPrev, s_btnNext, s_btnDismiss, s_btnArmyMinus, s_btnArmyPlus, s_btnMarch, s_btnClose, s_btnTgtPrev, s_btnTgtNext };
        for (int i = 0; i < 9; ++i) all[i]->eventMouseButtonClick += MyGUI::newDelegate(onButton);
        s_win->setVisible(false);
        wbLogAlways("[Warborn] court hall created");
    }
    catch (...) { s_win = NULL; wbLogAlways("[Warborn] court hall failed"); }
}

static void apply()
{
    bool dirty = false;
    EnterCriticalSection(&s_lock);
    if (s_viewDirty) { s_shown = s_view; s_viewDirty = false; dirty = true; }
    LeaveCriticalSection(&s_lock);
    if (!dirty) return;

    s_leader->setCaption(s_shown.leader);
    s_leaderLine->setCaption(s_shown.leaderLine);
    s_money->setCaption(s_shown.money);
    s_food->setCaption(s_shown.food);
    int fw = (int)(s_shown.foodFrac * (float)s_foodBarW); if (fw < 1) fw = 1;
    s_foodFill->setSize(fw, 10);
    s_foodFill->setColour(s_shown.foodFrac > 0.4f ? MyGUI::Colour(0.45f, 0.70f, 0.30f, 1.0f)
                        : s_shown.foodFrac > 0.15f ? MyGUI::Colour(0.85f, 0.70f, 0.25f, 1.0f) : MyGUI::Colour(0.80f, 0.25f, 0.20f, 1.0f));
    s_tax->setCaption(s_shown.tax);
    s_army->setCaption(s_shown.army);
    s_people->setCaption(s_shown.people);
    s_campaign->setCaption(s_shown.campaign);
    if (s_targetIdx >= (int)s_shown.targets.size()) s_targetIdx = 0;
    s_target->setCaption(s_shown.targets.empty() ? std::string("no enemy towns known") : "Target: " + s_shown.targets[s_targetIdx]);

    for (int o = 0; o < OFFICES && o < (int)s_shown.slots.size(); ++o)
    {
        const WbCourtSlotView& v = s_shown.slots[o];
        Card& c = s_cards[o];
        c.title->setCaption(v.title);
        c.name->setCaption(v.name);
        c.line->setCaption(v.line);
        c.name->setTextColour(v.filled ? MyGUI::Colour(0.92f, 0.88f, 0.80f, 1.0f) : MyGUI::Colour(0.55f, 0.52f, 0.46f, 1.0f));
        int bw = (int)(v.loyalty * (float)c.barW); if (bw < 1) bw = 1;
        c.barFill->setSize(bw, 10);
        c.barFill->setColour(v.loyalty > 0.55f ? MyGUI::Colour(0.40f, 0.72f, 0.30f, 1.0f)
                           : v.loyalty > 0.35f ? MyGUI::Colour(0.86f, 0.70f, 0.25f, 1.0f) : MyGUI::Colour(0.80f, 0.25f, 0.20f, 1.0f));
        c.barFill->setVisible(v.filled);
        for (int s = 0; s < 4; ++s) c.sel[s]->setVisible(o == s_selOffice);
    }

    int n = (int)s_shown.cands.size();
    if (s_page * PAGE >= n && s_page > 0) s_page = (n - 1) / PAGE;
    if (s_selCand >= n) s_selCand = -1;
    char t[64]; sprintf_s(t, sizeof(t), "CANDIDATES  (%d)", n);
    s_candTitle->setCaption(t);
    for (int i = 0; i < PAGE; ++i)
    {
        int idx = s_page * PAGE + i;
        if (idx < n)
        {
            s_candBtn[i]->setCaption(s_shown.cands[idx].label);
            s_candBtn[i]->setTextColour(idx == s_selCand ? MyGUI::Colour(1.0f, 0.85f, 0.35f, 1.0f) : MyGUI::Colour(0.86f, 0.82f, 0.74f, 1.0f));
            s_candBtn[i]->setVisible(true);
        }
        else s_candBtn[i]->setVisible(false);
    }
    s_btnPrev->setVisible(s_page > 0);
    s_btnNext->setVisible((s_page + 1) * PAGE < n);
    s_btnDismiss->setVisible(s_selOffice >= 0);
    if (s_selCand >= 0)       s_hint->setCaption("Now click an office card to appoint " + s_shown.cands[s_selCand].label.substr(0, s_shown.cands[s_selCand].label.find("  (")) + ".");
    else if (s_selOffice >= 0) s_hint->setCaption("Dismiss removes this officer. Or pick a candidate on the right to replace them.");
    else                      s_hint->setCaption("Pick a candidate on the right, then click an office card.");
}

void wbCourtUiToggle()
{
    lockInit();
    build();
    if (!s_win) return;
    bool show = !s_win->getVisible();
    s_win->setVisible(show);
    s_open = show;
    if (show) { s_rebuild = true; s_selCand = -1; s_selOffice = -1; MyGUI::LayerManager::getInstance().upLayerItem(s_win); }
}

void wbCourtUiFrame()
{
    if (!s_win || !s_win->getVisible()) return;
    apply();
}
