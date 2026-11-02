// Warborn: World - messenger toasts and the journal window.
// Copyright (C) 2026 Aungnaingthar. Licensed under the GNU GPLv3 (see LICENSE).
// Threads: the world (main loop) and the UI (player camera update) may run on
// different threads. The main loop builds every text; the UI thread only reads
// finished strings under a lock and reports clicks back through flags.

#include "Warborn.h"

#include <Debug.h>
#include <mygui/MyGUI.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <deque>
#include <string>

// ------------------------------------------------------------ text helpers
// MyGUI text boxes don't wrap: break long lines at spaces.
static std::string wrap(const std::string& in, size_t width)
{
    std::string out, line;
    size_t i = 0;
    while (i <= in.size())
    {
        size_t nl = in.find('\n', i);
        std::string para = in.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        std::string indent;
        size_t lead = para.find_first_not_of(' ');
        if (lead != std::string::npos && lead > 0) indent = para.substr(0, lead) + "    ";
        while (para.size() > width)
        {
            size_t cut = para.rfind(' ', width);
            if (cut == std::string::npos || cut < width / 3) cut = width;
            out += para.substr(0, cut) + "\n";
            para = indent + para.substr(cut + (para[cut] == ' ' ? 1 : 0));
        }
        out += para;
        if (nl == std::string::npos) break;
        out += "\n";
        i = nl + 1;
    }
    return out;
}

// ------------------------------------------------------------ shared state
struct WbLock
{
    CRITICAL_SECTION cs;
    WbLock()  { InitializeCriticalSection(&cs); }
    ~WbLock() { DeleteCriticalSection(&cs); }
};
static WbLock s_lock;
struct WbGuard
{
    WbGuard()  { EnterCriticalSection(&s_lock.cs); }
    ~WbGuard() { LeaveCriticalSection(&s_lock.cs); }
};

static std::deque<std::string> s_news;          // newest first (main thread only)
static std::string s_toastText;                 // locked
static ULONGLONG   s_toastUntil = 0;            // locked
static bool        s_toastDirty = false;        // locked
static std::string s_bodyText;                  // locked: journal text built by the main loop
static int         s_bodyOffers = 0;            // locked
static bool        s_bodyDirty  = false;        // locked
static volatile bool s_journalOpen = false;     // UI -> main
static volatile int  s_tab         = 0;         // UI -> main
static volatile int  s_acceptReq   = -1;        // UI -> main (button index)
static volatile bool s_rebuildNow  = true;      // UI -> main
static volatile bool s_hideReq     = false;     // main -> UI
static ULONGLONG     s_nextBuild   = 0;

void wbToast(const char* text)
{
    std::string t = text ? text : "";
    {
        WbGuard g;
        s_toastText = t;
        s_toastUntil = GetTickCount64() + 4500;
        s_toastDirty = true;
    }
    char b[300]; sprintf_s(b, sizeof(b), "[Warborn] toast: %s", t.c_str()); wbLog(b);
}

void wbNews(const char* text)
{
    if (!text) return;
    char stamp[32];
    sprintf_s(stamp, sizeof(stamp), "[%dm] ", (int)wbWorldMinutes());
    s_news.push_front(std::string(stamp) + text);
    while (s_news.size() > 40) s_news.pop_back();
    s_rebuildNow = true;
    std::string t = std::string("Messenger: ") + text;
    wbToast(t.c_str());
    std::string l = std::string("[Warborn] NEWS ") + text;
    wbLogAlways(l.c_str());
}

// ------------------------------------------------------------ widgets
static MyGUI::TextBox* s_toast      = NULL;
static bool            s_toastTried = false;
static MyGUI::Window*  s_win        = NULL;
static MyGUI::TextBox* s_body       = NULL;
static MyGUI::Button*  s_tabs[4]    = { NULL, NULL, NULL, NULL };
static MyGUI::Button*  s_accept[4]  = { NULL, NULL, NULL, NULL };
static bool            s_winTried   = false;
static const char* const TAB_NAME[4] = { "News", "Wars", "Contracts", "Reputation" };

static void ensureToast()
{
    if (s_toast || s_toastTried) return;
    s_toastTried = true;
    try
    {
        MyGUI::Gui* g = MyGUI::Gui::getInstancePtr();
        MyGUI::IntSize v = MyGUI::RenderManager::getInstance().getViewSize();
        if (g) s_toast = g->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText", v.width / 2 - 450, 70, 900, 24,
                                                         MyGUI::Align::Default, "Top", "wb_world_toast");
        if (s_toast)
        {
            s_toast->setTextAlign(MyGUI::Align::Center);
            s_toast->setTextColour(MyGUI::Colour(1.0f, 0.85f, 0.45f, 1.0f));
            s_toast->setNeedMouseFocus(false);
            s_toast->setVisible(false);
        }
    }
    catch (...) { s_toast = NULL; wbLogAlways("[Warborn] toast widget failed"); }
}

static void onTab(MyGUI::Widget* sender)
{
    for (int i = 0; i < 4; ++i) if (sender == s_tabs[i]) s_tab = i;
    s_rebuildNow = true;
}
static void onAccept(MyGUI::Widget* sender)
{
    for (int i = 0; i < 4; ++i)
        if (sender == s_accept[i]) { s_acceptReq = i; s_rebuildNow = true; }
}
static void onClose(MyGUI::Widget*) { if (s_win) s_win->setVisible(false); s_journalOpen = false; }

static void ensureWindow()
{
    if (s_win || s_winTried) return;
    s_winTried = true;
    try
    {
        MyGUI::Gui* g = MyGUI::Gui::getInstancePtr();
        if (!g) return;
        s_win = g->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", 0.18f, 0.12f, 0.64f, 0.72f,
                                                   MyGUI::Align::Center, "Window", "wb_world_journal");
        if (!s_win) return;
        s_win->setCaption("Warborn Journal");
        MyGUI::Widget* client = s_win->getClientWidget();
        for (int i = 0; i < 4; ++i)
        {
            s_tabs[i] = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.02f + 0.2f * (float)i, 0.01f, 0.19f, 0.07f,
                                                                MyGUI::Align::Default, "");
            s_tabs[i]->setCaption(TAB_NAME[i]);
            s_tabs[i]->eventMouseButtonClick += MyGUI::newDelegate(onTab);
        }
        MyGUI::Button* close = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.84f, 0.01f, 0.14f, 0.07f,
                                                                      MyGUI::Align::Default, "");
        close->setCaption("Close");
        close->eventMouseButtonClick += MyGUI::newDelegate(onClose);
        s_body = client->createWidgetReal<MyGUI::TextBox>("Kenshi_TextboxStandardText_Small", 0.03f, 0.11f, 0.94f, 0.76f,
                                                         MyGUI::Align::Default, "");
        s_body->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
        s_body->setNeedMouseFocus(false);
        for (int i = 0; i < 4; ++i)
        {
            s_accept[i] = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.02f + 0.245f * (float)i, 0.89f, 0.23f, 0.08f,
                                                                  MyGUI::Align::Default, "");
            char b[24]; sprintf_s(b, sizeof(b), "Accept %d", i + 1);
            s_accept[i]->setCaption(b);
            s_accept[i]->eventMouseButtonClick += MyGUI::newDelegate(onAccept);
            s_accept[i]->setVisible(false);
        }
        s_win->setVisible(false);
        wbLogAlways("[Warborn] journal window created");
    }
    catch (...) { s_win = NULL; wbLogAlways("[Warborn] journal window failed"); }
}

static std::string newsText()
{
    if (s_news.empty()) return "No news has reached you yet.\n";
    std::string out;
    for (size_t i = 0; i < s_news.size(); ++i) { out += s_news[i]; out += "\n"; }
    return out;
}

// Main thread: build the text of the open tab.
static void buildBody()
{
    std::string t;
    switch (s_tab)
    {
        case 0: t = "NEWS FROM MESSENGERS AND TOWN CRIERS\n\n" + newsText(); break;
        case 1: t = "WARS\n" + wbWorldSummary() + "\nPACTS\n" + wbDiplomacySummary()
                  + "\nTHE POWERS OF THE LAND (towns held)\n" + wbPowerRanking(); break;
        case 2: t = wbContractsSummary(); break;
        default: t = "YOUR STANDING (how each faction sees you)\n" + wbReputationSummary()
                   + "\nEarn respect by felling a faction's enemies, fighting in their wars and finishing their contracts.\n"
                   + "Good standing makes their people willing to join you (select one, press the recruit key)."; break;
    }
    std::string wrapped = wrap(t, 110);
    int offers = wbContractsOfferCount();
    WbGuard g;
    s_bodyText = wrapped; s_bodyOffers = offers; s_bodyDirty = true;
}

// UI thread: show the finished text.
static void applyBody()
{
    if (!s_body) return;
    std::string t; int offers = 0; bool dirty = false;
    {
        WbGuard g;
        if (s_bodyDirty) { t = s_bodyText; offers = s_bodyOffers; dirty = true; s_bodyDirty = false; }
    }
    if (!dirty) return;
    s_body->setCaption(t);
    for (int i = 0; i < 4; ++i) if (s_accept[i]) s_accept[i]->setVisible(s_tab == 2 && i < offers);
    for (int i = 0; i < 4; ++i) if (s_tabs[i]) s_tabs[i]->setStateSelected(i == s_tab);
}

// Main thread, once per frame: accept clicks, rebuild the open tab.
void wbUiMainFrame()
{
    int a = s_acceptReq;
    if (a >= 0) { s_acceptReq = -1; wbContractsAccept(a); s_rebuildNow = true; }
    if (!s_journalOpen) return;
    ULONGLONG now = GetTickCount64();
    if (s_rebuildNow || now >= s_nextBuild)
    {
        s_rebuildNow = false;
        s_nextBuild = now + 1000;
        buildBody();
    }
}

void wbJournalToggle()
{
    ensureWindow();
    if (!s_win) return;
    bool show = !s_win->getVisible();
    s_win->setVisible(show);
    s_journalOpen = show;
    if (show) { s_rebuildNow = true; MyGUI::LayerManager::getInstance().upLayerItem(s_win); }
}

void wbUiFrame()
{
    ensureToast();
    ULONGLONG now = GetTickCount64();
    if (s_hideReq) { s_hideReq = false; if (s_win) s_win->setVisible(false); s_journalOpen = false; }
    if (s_toast)
    {
        std::string text; bool dirty = false; ULONGLONG until = 0;
        {
            WbGuard g;
            until = s_toastUntil;
            if (s_toastDirty) { text = s_toastText; dirty = true; s_toastDirty = false; }
        }
        bool want = now < until;
        if (dirty) s_toast->setCaption(text);
        if (want)
        {
            MyGUI::IntSize v = MyGUI::RenderManager::getInstance().getViewSize();
            s_toast->setPosition(v.width / 2 - 450, 70);
        }
        if (want != s_toast->getVisible()) s_toast->setVisible(want);
    }
    if (s_win && s_win->getVisible()) applyBody();
}

void wbUiReset()
{
    s_news.clear();
    { WbGuard g; s_toastUntil = 0; s_bodyDirty = false; }
    s_hideReq = true;
}
