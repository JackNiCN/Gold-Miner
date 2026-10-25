// ============================================================
//  Gold Miner v5.6 - C++11 兼容完整版
//  - 音效异步化（不阻塞主循环）
//  - 结算画面：百分比 + 星级 + 评级 + 详细统计
//  - 修复普通模式消息框缩进
//  - 修复无尽模式 BUFF 选择框对齐
//  - 兼容 C++11（无结构化绑定 / 无 make_unique / 无 if-init）
//  编译: g++ -std=c++11 -O2 -Wall -Wextra -pthread -o goldminer goldminer.cpp
//  运行: ./goldminer [--no-color] [--no-sound] [--replay SEED] [--reset-save]
// ============================================================
#define _CRT_SECURE_NO_WARNINGS
#include <bits/stdc++.h>
#include <chrono>
#include <thread>
#include <random>
#include <array>
#include <cmath>
#include <atomic>
#include <mutex>
#include <cstdint>

#ifdef _WIN32
  #include <conio.h>
  #include <windows.h>
//  #pragma comment(lib, "winmm.lib")
#else
  #include <termios.h>
  #include <unistd.h>
  #include <sys/select.h>
  #include <sys/wait.h>
  #include <fcntl.h>
  #ifdef __APPLE__
    #include <AudioToolbox/AudioToolbox.h>
  #endif
#endif
 
using namespace std;

// ================== 兼容 clamp ==================
template <typename T>
inline T clampVal(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ================== 时间戳 ==================
inline string formatTimestamp(long long t) {
    if (t <= 0) return "---";
    time_t tt = (time_t)t;
    tm* lt = localtime(&tt);
    if (!lt) return "---";
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min);
    return buf;
}

// ================== 常量 ==================
const int    H              = 22;
const int    W              = 60;
const int    MAX_LEVEL      = 50;
const int    SAVE_VERSION   = 8;
const int    NUM_UPGRADES   = 12;
const int    MSG_MAX        = 10;
const int    MSG_WIDTH      = 14;
const double MSG_LIFE       = 6.0;
const int    FRAME_MS       = 16;
const int    TOP_SCORES     = 5;
const int    MAX_BUFFS      = 16;

const double HOOK_EXTEND_SPEED  = 32.0;
const double HOOK_RETRACT_SPEED = 22.0;
const double SWING_SPEED        = 1.8;
const double SWING_MAX_ANGLE    = 1.50;
const double GRAB_RADIUS        = 1.3;
const double HOOK_MIN_LEN       = 3.0;
const double DT_MAX             = 0.10;
const double SUBSTEP_MIN        = 0.15;
const int    TIME_WARN          = 10;

const double BOMB_REDUCE_CAP    = 0.60;
const int    BOMB_BONUS_CAP     = 60;
const int    BOMB_DAMAGE_FLOOR  = 30;

const string SAVE_FILE = "goldminer_save.txt";

// ================== 成就 ==================
enum AchId {
    A_DIAMOND_HUNTER = 0,
    A_GOLD_RUSH,
    A_STEADY_HAND,
    A_RISING_STAR,
    A_GOLD_BARON,
    A_DEEP_DIVER,
    A_LUCKY_STRIKE,
    A_PERFECTIONIST,
    A_SPEEDRUNNER,
    A_UNTOUCHABLE,
    A_DIAMOND_HOARD,
    A_MAXED_OUT,
    A_CRAB_HUNTER,
    A_LEVEL_10,
    A_LEVEL_20,
    A_LEVEL_30,
    A_ENDLESS_5,
    A_ENDLESS_10,
    A_ENDLESS_20,
    A_NO_ROCK_CLEAR,
    A_5_DIAMONDS_ONE,
    A_10_GOLDS_ONE,
    A_COMBO_10,
    A_COMBO_20,
    A_ALL_UPGRADES,
    A_10000_ONE_LEVEL,
    A_TIME_LOW_WIN,
    A_BOMB_LOVER,
    A_MOUSE_HUNTER,
    A_NO_MISS_5,
    A_NUM_ACH
};

struct AchievementDef {
    const char* name;
    const char* desc;
    bool        hidden;
};

const AchievementDef ACHS[A_NUM_ACH] = {
    {"Diamond Hunter", "Catch your first diamond",        false},
    {"Gold Rush",      "Catch 5 golds in one level",      false},
    {"Steady Hand",    "Grab 10 items without a bomb",    false},
    {"Rising Star",    "Accumulate 1000 gold",            false},
    {"Gold Baron",     "Accumulate 10000 gold",           false},
    {"Deep Diver",     "Reach Level 5",                   false},
    {"Lucky Strike",   "Trigger 10 critical hits",        false},
    {"Perfectionist",  "Score 150% of target",            false},
    {"Speedrunner",    "Clear a level in 30s",            false},
    {"Untouchable",    "Clear a level without a bomb",    false},
    {"Diamond Hoard",  "Catch 5 diamonds total",          false},
    {"Maxed Out",      "Max any upgrade",                 false},
    {"Crab Hunter",    "Catch 3 crabs total",             false},
    {"Level 10",       "Reach Level 10",                  false},
    {"Level 20",       "Reach Level 20",                  false},
    {"Level 30",       "Reach Level 30",                  false},
    {"Endless Diver",  "Reach Endless wave 5",            false},
    {"Endless Master", "Reach Endless wave 10",           false},
    {"Endless Legend", "Reach Endless wave 20",           false},
    {"Pure Hands",     "Clear a level without rocks",     false},
    {"Gem Collector",  "Catch 5 diamonds in one level",   false},
    {"Gold Rush+",     "Catch 10 golds in one level",     false},
    {"Combo 10",       "Reach a 10x combo",               false},
    {"Combo 20",       "Reach a 20x combo",               false},
    {"Completionist",  "Max all upgrades",                true },
    {"Over 9000",      "Score 10000 in one level",        false},
    {"Last Second",    "Win with less than 3s left",      false},
    {"Bomb Enthusiast","Win a level after catching 3 bombs", false},
    {"Mouse Hunter",   "Catch 10 mice total",             false},
    {"Sharpshooter",   "Hit 5 times in a row",            false},
};

// ================== BUFF 定义 ==================
enum BuffId {
    BUFF_SPEED_UP = 0,
    BUFF_POWER_UP,
    BUFF_TIME_UP,
    BUFF_CRIT_UP,
    BUFF_GOLD_UP,
    BUFF_MAGNET_UP,
    BUFF_BOMB_SAFE,
    BUFF_NO_TIDE,
    BUFF_DOUBLE_DIAMOND,
    BUFF_EXTRA_DIAMOND,
    BUFF_EXTRA_TIME,
    BUFF_REFLECT,
    BUFF_COUNT
};

struct BuffDef {
    BuffId id;
    const char* name;
    const char* desc;
    int rarity;   // 1=普通 2=稀有 3=史诗
};

const BuffDef BUFFS[] = {
    {BUFF_SPEED_UP,       "Swift Hook",    "Hook speed +25%",                  1},
    {BUFF_POWER_UP,       "Strong Arm",    "Weight penalty -40%",              1},
    {BUFF_TIME_UP,        "Clock Bonus",   "Time +15s",                        1},
    {BUFF_CRIT_UP,        "Sharp Eye",     "Crit rate +20%",                   2},
    {BUFF_GOLD_UP,        "Midas Touch",   "Gold gain +30%",                   2},
    {BUFF_MAGNET_UP,      "Wide Hook",     "Grab radius +0.5",                 2},
    {BUFF_BOMB_SAFE,      "Bomb Suit",     "Bomb damage -50%",                 2},
    {BUFF_NO_TIDE,        "Calm Sea",      "Tide disabled this run",           2},
    {BUFF_DOUBLE_DIAMOND, "Gem Focus",     "Diamond value x2",                 3},
    {BUFF_EXTRA_DIAMOND,  "Gem Rain",      "+2 diamonds each wave",            3},
    {BUFF_EXTRA_TIME,     "Slow Motion",   "Time +30s",                        3},
    {BUFF_REFLECT,        "Mirror Shield", "Bombs give +50 instead of damage", 3},
};
const int NUM_BUFFS = (int)(sizeof(BUFFS) / sizeof(BUFFS[0]));

// ================== 音效（异步） ==================
static bool g_soundOn = true;

namespace audio {

#ifdef _WIN32
// Windows 平台
inline void playTone(int freq, int ms) {
    if (!g_soundOn) return;
    if (ms <= 0) return;

    static std::atomic<bool> busy(false);
    if (busy.exchange(true) == true) return;

    int f = freq, m = ms;
    std::thread([f, m]() {
        Beep((DWORD)f, (DWORD)m);
        busy.store(false);
    }).detach();
}
#elif defined(__APPLE__)
// macOS 平台
inline void playTone(int freq, int ms) {
    if (!g_soundOn) return;
    (void)freq;
    if (ms > 0) AudioServicesPlaySystemSound(1057);
}
#else
// Linux 平台
inline bool commandExists(const char* cmd) {
    string s = string("command -v ") + cmd + " >/dev/null 2>&1";
    return system(s.c_str()) == 0;
}

inline int detectPlayer() {
    static int cached = -1;
    if (cached != -1) return cached;
    if (commandExists("paplay"))      cached = 1;
    else if (commandExists("aplay"))  cached = 2;
    else                              cached = 0;
    return cached;
}

inline void writeWavFile(const string& path, int freq, int ms) {
    int sampleRate = 8000;
    int samples = sampleRate * ms / 1000;
    if (samples <= 0) return;

    vector<int16_t> pcm(samples);
    for (int i = 0; i < samples; i++) {
        double t = (double)i / sampleRate;
        double env = 1.0 - (double)i / samples;
        pcm[i] = (int16_t)(sin(2 * M_PI * freq * t) * 16000 * env);
    }

    ofstream f(path.c_str(), ios::binary);
    if (!f) return;

    uint32_t v32;
    uint16_t v16;

    auto w32 = [&](uint32_t v) { f.write((char*)&v, 4); };
    auto w16 = [&](uint16_t v) { f.write((char*)&v, 2); };
    uint32_t dataSize = (uint32_t)(pcm.size() * 2);

    f.write("RIFF", 4);
    v32 = 36 + dataSize; f.write((char*)&v32, 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    v32 = 16; f.write((char*)&v32, 4);
    v16 = 1;  f.write((char*)&v16, 2);
    v16 = 1;  f.write((char*)&v16, 2);
    v32 = sampleRate;      f.write((char*)&v32, 4);
    v32 = sampleRate * 2;  f.write((char*)&v32, 4);
    v16 = 2;  f.write((char*)&v16, 2);
    v16 = 16; f.write((char*)&v16, 2);
    f.write("data", 4);
    f.write((char*)&dataSize, 4);
    f.write((char*)pcm.data(), dataSize);
    (void)w32; (void)w16;
}

inline void playTone(int freq, int ms) {
    if (!g_soundOn) return;
    if (ms <= 0) return;

    int player = detectPlayer();
    if (player == 0) { cout << '\a' << flush; return; }

    static std::atomic<bool> busy(false);
    if (busy.exchange(true) == true) return;

    static std::atomic<int> counter(0);
    int id = counter.fetch_add(1);
    string wavPath = "/tmp/goldminer_beep_" + to_string(id % 4) + ".wav";

    std::thread([freq, ms, player, wavPath]() {
        writeWavFile(wavPath, freq, ms);
        string cmd = (player == 1)
            ? "paplay " + wavPath + " >/dev/null 2>&1"
            : "aplay -q " + wavPath + " >/dev/null 2>&1";
        system(cmd.c_str());
        busy.store(false);
    }).detach();
}
#endif

// 播放和弦（异步）
inline void playChord(const vector<pair<int,int> >& notes) {
    if (!g_soundOn) return;

#ifdef _WIN32
    static std::atomic<bool> busy(false);
    if (busy.exchange(true) == true) return;
    vector<pair<int,int> > copy = notes;
    std::thread([copy]() {
        for (size_t i = 0; i < copy.size(); i++) {
            int f = copy[i].first;
            int m = copy[i].second;
            Beep((DWORD)f, (DWORD)m);
        }
        busy.store(false);
    }).detach();
#elif defined(__APPLE__)
    (void)notes;
    AudioServicesPlaySystemSound(1057);
#else
    static std::atomic<bool> busy(false);
    if (busy.exchange(true) == true) return;
    vector<pair<int,int> > copy = notes;
    std::thread([copy]() {
        int player = detectPlayer();
        if (player == 0) {
            cout << '\a' << flush;
            busy.store(false);
            return;
        }
        for (size_t i = 0; i < copy.size(); i++) {
            int f = copy[i].first;
            int m = copy[i].second;
            string wavPath = "/tmp/goldminer_chord_" + to_string(i) + ".wav";
            writeWavFile(wavPath, f, m);
            string cmd = (player == 1)
                ? "paplay " + wavPath + " >/dev/null 2>&1"
                : "aplay -q " + wavPath + " >/dev/null 2>&1";
            system(cmd.c_str());
        }
        busy.store(false);
    }).detach();
#endif
}

inline void sndEat()   { playTone(880,  60); }
inline void sndCrit()  { playTone(1320, 90); }
inline void sndBomb()  { playTone(220,  200); }
inline void sndTide()  { playTone(440,  150); }
inline void sndAch()   { playTone(1568, 120); }
inline void sndBuy()   { playTone(1046, 80); }
inline void sndWin()   {
    vector<pair<int,int> > notes;
    notes.push_back(make_pair(1318, 120));
    notes.push_back(make_pair(1568, 120));
    notes.push_back(make_pair(2093, 180));
    playChord(notes);
}
inline void sndLose()  { playTone(392,  300); }
inline void sndMenu()  { playTone(660,  40); }

} // namespace audio

using audio::sndEat;
using audio::sndCrit;
using audio::sndBomb;
using audio::sndTide;
using audio::sndAch;
using audio::sndBuy;
using audio::sndWin;
using audio::sndLose;
using audio::sndMenu;

// ================== 颜色 & ANSI ==================
enum Color {
    FG_RED=31, FG_GREEN=32, FG_YELLOW=33, FG_BLUE=34,
    FG_MAGENTA=35, FG_CYAN=36, FG_WHITE=37,
    FG_GRAY=90, FG_LRED=91, FG_LGREEN=92, FG_LYELLOW=93,
    FG_LBLUE=94, FG_LMAGENTA=95, FG_LCYAN=96, FG_LWHITE=97
};

static bool g_noColor = false;

inline string col(int c) {
    if (g_noColor) return "";
    return "\033[" + to_string(c) + "m";
}
inline string cur(int r, int c) {
    if (g_noColor) return "";
    return "\033[" + to_string(r) + ";" + to_string(c) + "H";
}
const string RESET    = "\033[0m";
const string CLEAR    = "\033[2J\033[H";
const string HIDE_CUR = "\033[?25l";
const string SHOW_CUR = "\033[?25h";
const string CLR_LINE = "\033[K";

// ================== 跨平台输入 ==================
enum Key {
    KEY_NONE = 0, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_ENTER, KEY_SPACE, KEY_ESC, KEY_TAB, KEY_CHAR, KEY_UNKNOWN
};
struct KeyEvent {
    Key  key;
    char ch;
    KeyEvent() : key(KEY_NONE), ch(0) {}
};

#ifdef _WIN32
namespace plat {
    inline void termInit()  { system("chcp 65001 > nul"); }
    inline void termRestore() {}
    inline bool kbhit()     { return _kbhit() != 0; }
    inline int  getchRaw()  { return _getch(); }
    inline int  waitByte(int) {
        if (_kbhit()) return _getch();
        return -1;
    }
}
#else
namespace plat {
    static termios g_saved;
    static bool    g_savedOk = false;

    inline void termInit() {
        if (g_savedOk) return;
        tcgetattr(STDIN_FILENO, &g_saved);
        termios t = g_saved;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN]  = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        g_savedOk = true;
    }
    inline void termRestore() {
        if (g_savedOk) tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
    }
    inline bool kbhit() {
        termInit();
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
        return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
    }
    inline int getchRaw() {
        termInit();
        unsigned char c = 0;
        if (read(STDIN_FILENO, &c, 1) == 1) return c;
        return -1;
    }
    inline int waitByte(int ms) {
        termInit();
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        timeval tv;
        tv.tv_sec  = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            unsigned char c = 0;
            if (read(STDIN_FILENO, &c, 1) == 1) return c;
        }
        return -1;
    }
}
#endif

inline KeyEvent readKey() {
    KeyEvent ev;
    if (!plat::kbhit()) return ev;

    int c = plat::getchRaw();
    if (c < 0) return ev;

#ifdef _WIN32
    if (c == 0 || c == 224) {
        int c2 = plat::getchRaw();
        switch (c2) {
            case 72: ev.key = KEY_UP;    break;
            case 80: ev.key = KEY_DOWN;  break;
            case 75: ev.key = KEY_LEFT;  break;
            case 77: ev.key = KEY_RIGHT; break;
            default: ev.key = KEY_UNKNOWN; break;
        }
        return ev;
    }
#else
    if (c == 27) {
        int c2 = plat::waitByte(40);
        if (c2 < 0) { ev.key = KEY_ESC; return ev; }
        if (c2 == '[' || c2 == 'O') {
            int c3 = plat::waitByte(40);
            switch (c3) {
                case 'A': ev.key = KEY_UP;    break;
                case 'B': ev.key = KEY_DOWN;  break;
                case 'C': ev.key = KEY_RIGHT; break;
                case 'D': ev.key = KEY_LEFT;  break;
                default:  ev.key = KEY_UNKNOWN; break;
            }
        } else {
            ev.key = KEY_ESC;
        }
        return ev;
    }
#endif

    switch (c) {
        case 13: case 10: ev.key = KEY_ENTER; return ev;
        case ' ':         ev.key = KEY_SPACE; return ev;
        case 9:           ev.key = KEY_TAB;   return ev;
        case 27:          ev.key = KEY_ESC;   return ev;
        default:
            ev.key = KEY_CHAR;
            ev.ch  = (char)tolower(c);
            return ev;
    }
}

inline void sleepMs(int ms) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}

// ================== 随机数 ==================
static mt19937 g_rng((uint32_t)chrono::steady_clock::now().time_since_epoch().count());
inline void seedRng(uint32_t s) { g_rng.seed(s); }
inline int randInt(int lo, int hi) {
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    uniform_int_distribution<int> d(lo, hi);
    return d(g_rng);
}
inline double randDouble(double lo, double hi) {
    uniform_real_distribution<double> d(lo, hi);
    return d(g_rng);
}
inline bool chance(double p) {
    if (p <= 0) return false;
    if (p >= 1) return true;
    uniform_real_distribution<double> d(0.0, 1.0);
    return d(g_rng) < p;
}
inline double nowSeconds() {
    return chrono::duration<double>(
        chrono::steady_clock::now().time_since_epoch()).count();
}

// ================== 物品定义 ==================
enum MoveType { MV_STATIC = 0, MV_CRAB, MV_MOUSE };

struct ItemDef {
    const char* name;
    char        ch;
    int         color;
    int         value;
    double      weight;
    MoveType    move;
    int         spawnBase;
    int         spawnRand;
    bool        pulse;
};

enum ItemKind {
    IK_BIG_GOLD = 0, IK_MID_GOLD, IK_SMALL_GOLD, IK_DIAMOND,
    IK_ROCK, IK_BOMB, IK_CRAB, IK_MOUSE,
    IK_COUNT
};

const ItemDef ITEM_DEFS[IK_COUNT] = {
    {"BigGold",   '@', FG_LYELLOW,  2550, 0.5, MV_STATIC, 2, 1, true },
    {"MidGold",   'O', FG_YELLOW,   120, 3.0, MV_STATIC, 3, 1, false},
    {"SmallGold", 'o', FG_YELLOW,    60, 1.5, MV_STATIC, 4, 2, false},
    {"Diamond",   '*', FG_LCYAN,    500, 0.5, MV_STATIC, 1, 1, true },
    {"Rock",      '#', FG_GRAY,      10, 7.0, MV_STATIC, 4, 2, false},
    {"Bomb",      'B', FG_LRED,    -100, 2.0, MV_STATIC, 1, 1, false},
    {"Crab",      'C', FG_LRED,     180, 2.0, MV_CRAB,   1, 1, false},
    {"Mouse",     'r', FG_GRAY,     120, 1.0, MV_MOUSE,  1, 0, false},
};

inline const ItemDef& defOf(char ch) {
    for (int i = 0; i < IK_COUNT; i++)
        if (ITEM_DEFS[i].ch == ch) return ITEM_DEFS[i];
    return ITEM_DEFS[IK_ROCK];
}

// ================== 结构体 ==================
struct Item {
    double x, y;
    int    value;
    double weight;
    char   ch;
    int    color;
    bool   alive;
    bool   grabbedFlag;
    bool   pulse;
    MoveType mvType;
    int    mvDir;
    double mvTimer;

    Item()
        : x(0), y(0), value(0), weight(1.0), ch('?'), color(FG_WHITE),
          alive(true), grabbedFlag(false), pulse(false),
          mvType(MV_STATIC), mvDir(0), mvTimer(0) {}
};

struct MsgLine {
    string text;
    int    color;
    double life;
    int    count;
    MsgLine() : color(FG_WHITE), life(MSG_LIFE), count(1) {}
};

struct FloatingText {
    double x, y;
    string text;
    int    color;
    double life;
    double vy;
    FloatingText() : x(0), y(0), color(FG_WHITE), life(1.2), vy(-6.0) {}
};

struct UpgradeDef {
    const char* name;
    const char* desc;
    int basePrice, priceStep, maxLv;
};

const UpgradeDef UPG[NUM_UPGRADES] = {
    {"Speed   ", "Hook speed +10%",        300, 400, 5},
    {"Power   ", "Weight penalty -8%",     300, 400, 5},
    {"Time    ", "Extra +8s per level",    300, 400, 5},
    {"Shield  ", "Bomb damage -12%",       300, 400, 5},
    {"Magnet  ", "Grab radius +0.2",       300, 400, 5},
    {"Luck    ", "Item value +10%",        500, 500, 5},
    {"Crit    ", "Crit rate +8% (x1.5)",   500, 500, 5},
    {"Wealth  ", "Gold gain +15%",         500, 500, 5},
    {"Bomb+   ", "Bomb penalty -12",       500, 500, 5},
    {"SlowTide", "Tide interval +3s",      500, 500, 5},
    {"Rich    ", "+1 small gold spawned",  500, 500, 5},
    {"Safety  ", "-1 bomb spawned",        600, 600, 3},
};

// ================== 存档 ==================
struct ScoreEntry {
    int       score;
    int       level;
    long long when;
    ScoreEntry() : score(0), level(0), when(0) {}
};

struct Stats {
    int       maxGold;
    long long totalPlayMs;
    int       totalGrabbed;
    int       diamondCount;
    int       crabCount;
    int       mouseCount;
    int       endlessBest;
    int       endlessCurrent;
    int       bestScore[MAX_LEVEL + 1];

    Stats() : maxGold(0), totalPlayMs(0), totalGrabbed(0),
              diamondCount(0), crabCount(0), mouseCount(0),
              endlessBest(0), endlessCurrent(0) {
        for (int i = 0; i <= MAX_LEVEL; i++) bestScore[i] = 0;
    }
};

struct SaveData {
    int  version;
    int  level;
    int  gold;
    bool soundOn;
    int  upLv[NUM_UPGRADES];
    Stats stats;
    bool unlocked[A_NUM_ACH];
    ScoreEntry topScores[TOP_SCORES];
    uint8_t endlessBuffs[MAX_BUFFS];
    uint8_t endlessBuffCount;

    SaveData() : version(SAVE_VERSION), level(1), gold(0),
                 soundOn(true), endlessBuffCount(0) {
        for (int i = 0; i < NUM_UPGRADES; i++) upLv[i] = 0;
        for (int i = 0; i < A_NUM_ACH; i++) unlocked[i] = false;
        for (int i = 0; i < MAX_BUFFS; i++) endlessBuffs[i] = 0;
    }
};

SaveData newSave() { return SaveData(); }

void clampUpgrades(SaveData& d) {
    for (int i = 0; i < NUM_UPGRADES; i++)
        d.upLv[i] = clampVal(d.upLv[i], 0, UPG[i].maxLv);
    d.level = clampVal(d.level, 1, MAX_LEVEL);
    if (d.gold < 0) d.gold = 0;
    if (d.endlessBuffCount > MAX_BUFFS) d.endlessBuffCount = MAX_BUFFS;
    for (int i = 0; i < d.endlessBuffCount; i++) {
        if (d.endlessBuffs[i] >= (uint8_t)BUFF_COUNT)
            d.endlessBuffs[i] = 0;
    }
}

void recordScore(SaveData& s, int score, int level) {
    if (score <= 0) return;
    ScoreEntry e;
    e.score = score;
    e.level = level;
    e.when  = (long long)time(NULL);
    for (int i = 0; i < TOP_SCORES; i++) {
        if (e.score > s.topScores[i].score) {
            for (int j = TOP_SCORES - 1; j > i; j--)
                s.topScores[j] = s.topScores[j-1];
            s.topScores[i] = e;
            return;
        }
    }
}

void migrateSave(SaveData& d, int fromVer) {
    if (fromVer <= 5) {
        for (int i = 0; i < TOP_SCORES; i++) d.topScores[i] = ScoreEntry();
    }
    if (fromVer <= 6) {
        d.stats.mouseCount      = 0;
        d.stats.endlessBest     = 0;
        d.stats.endlessCurrent  = 0;
    }
    if (fromVer <= 7) {
        d.stats.endlessCurrent  = 0;
    }
    d.version = SAVE_VERSION;
}

bool backupFile(const string& src, const string& dst) {
    ifstream in(src.c_str(), ios::binary);
    if (!in) return false;
    ofstream out(dst.c_str(), ios::binary);
    if (!out) return false;
    out << in.rdbuf();
    return true;
}

enum SaveStatus { SAVE_OK, SAVE_NOT_FOUND, SAVE_PARSE_ERROR };

struct SaveResult {
    SaveStatus status;
    SaveData   data;
    int        loadedVersion;
    SaveResult() : status(SAVE_NOT_FOUND), loadedVersion(0) {}
};

SaveResult loadSaveEx() {
    SaveResult r;
    r.data = newSave();

    ifstream f(SAVE_FILE.c_str());
    if (!f) { r.status = SAVE_NOT_FOUND; return r; }

    string line;
    if (!getline(f, line)) { r.status = SAVE_PARSE_ERROR; return r; }

    {
        istringstream is(line);
        string k; int v = 0;
        if (!(is >> k >> v)) { r.status = SAVE_PARSE_ERROR; return r; }
        if (k != "version")  { r.status = SAVE_PARSE_ERROR; return r; }
        if (v < 1 || v > SAVE_VERSION) {
            r.status = SAVE_PARSE_ERROR;
            r.loadedVersion = v;
            return r;
        }
        r.loadedVersion = v;
        r.data.version  = v;
    }

    // 读取整数行：格式 "key v1 v2 v3..."
    // C++11 兼容版：不用 lambda 捕获引用，改用普通函数/循环
    vector<string> lines;
    while (getline(f, line)) lines.push_back(line);

    auto findLine = [&](const string& key) -> int {
        for (size_t i = 0; i < lines.size(); i++) {
            istringstream is(lines[i]);
            string k;
            if ((is >> k) && k == key) return (int)i;
        }
        return -1;
    };

    auto parseInts = [&](int idx, vector<long long>& out) {
        if (idx < 0) return;
        istringstream is(lines[idx]);
        string k;
        is >> k;
        long long v;
        while (is >> v) out.push_back(v);
    };

    {
        int idx = findLine("core");
        if (idx >= 0) {
            vector<long long> v;
            parseInts(idx, v);
            if (v.size() >= 3) {
                r.data.level   = (int)v[0];
                r.data.gold    = (int)v[1];
                r.data.soundOn = (v[2] != 0);
            }
        }
    }
    {
        int idx = findLine("upg");
        if (idx >= 0) {
            vector<long long> v;
            parseInts(idx, v);
            for (int i = 0; i < NUM_UPGRADES && i < (int)v.size(); i++)
                r.data.upLv[i] = (int)v[i];
        }
    }
    {
        int idx = findLine("stats");
        if (idx >= 0) {
            vector<long long> v;
            parseInts(idx, v);
            if (v.size() >= 5) {
                r.data.stats.maxGold      = (int)v[0];
                r.data.stats.totalPlayMs  = v[1];
                r.data.stats.totalGrabbed = (int)v[2];
                r.data.stats.diamondCount = (int)v[3];
                r.data.stats.crabCount    = (int)v[4];
                if (v.size() >= 7) {
                    r.data.stats.mouseCount  = (int)v[5];
                    r.data.stats.endlessBest = (int)v[6];
                }
                if (v.size() >= 8) {
                    r.data.stats.endlessCurrent = (int)v[7];
                }
            }
        }
    }
    {
        int idx = findLine("best");
        if (idx >= 0) {
            vector<long long> v;
            parseInts(idx, v);
            for (int i = 0; i < MAX_LEVEL + 1 && i < (int)v.size(); i++)
                r.data.stats.bestScore[i] = (int)v[i];
        }
    }
    {
        int idx = findLine("ach");
        if (idx >= 0) {
            vector<long long> v;
            parseInts(idx, v);
            for (int i = 0; i < A_NUM_ACH && i < (int)v.size(); i++)
                r.data.unlocked[i] = (v[i] != 0);
        }
    }
    {
        int idx = findLine("top");
        if (idx >= 0) {
            vector<long long> v;
            parseInts(idx, v);
            for (int i = 0; i < TOP_SCORES; i++) {
                int base = i * 3;
                if (base + 2 < (int)v.size()) {
                    r.data.topScores[i].score = (int)v[base + 0];
                    r.data.topScores[i].level = (int)v[base + 1];
                    r.data.topScores[i].when  = v[base + 2];
                }
            }
        }
    }
    {
        int idx = findLine("buffs");
        if (idx >= 0) {
            vector<long long> v;
            parseInts(idx, v);
            if (!v.empty()) {
                int n = (int)v[0];
                if (n > MAX_BUFFS) n = MAX_BUFFS;
                for (int i = 0; i < n; i++)
                    r.data.endlessBuffs[i] = (uint8_t)v[i + 1];
                r.data.endlessBuffCount = (uint8_t)n;
            }
        }
    }

    clampUpgrades(r.data);
    r.status = SAVE_OK;
    return r;
}

void writeSave(const SaveData& d) {
    ofstream f(SAVE_FILE.c_str());
    if (!f) return;
    f << "version " << SAVE_VERSION << "\n";
    f << "core " << d.level << " " << d.gold << " " << (d.soundOn ? 1 : 0) << "\n";
    f << "upg";
    for (int i = 0; i < NUM_UPGRADES; i++) f << " " << d.upLv[i];
    f << "\n";
    f << "stats " << d.stats.maxGold << " " << d.stats.totalPlayMs << " "
      << d.stats.totalGrabbed << " " << d.stats.diamondCount << " "
      << d.stats.crabCount << " " << d.stats.mouseCount << " "
      << d.stats.endlessBest << " " << d.stats.endlessCurrent << "\n";
    f << "best";
    for (int i = 0; i < MAX_LEVEL + 1; i++) f << " " << d.stats.bestScore[i];
    f << "\n";
    f << "ach";
    for (int i = 0; i < A_NUM_ACH; i++) f << " " << (d.unlocked[i] ? 1 : 0);
    f << "\n";
    f << "top";
    for (int i = 0; i < TOP_SCORES; i++)
        f << " " << d.topScores[i].score
          << " " << d.topScores[i].level
          << " " << d.topScores[i].when;
    f << "\n";
    f << "buffs " << (int)d.endlessBuffCount;
    for (int i = 0; i < d.endlessBuffCount; i++)
        f << " " << (int)d.endlessBuffs[i];
    f << "\n";
}

// ================== 升级效果 ==================
struct UpgradeEffects {
    double speedMul;
    double powerBonus;
    double timeBonus;
    double bombReduce;
    double magnet;
    double luckMul;
    double critChance;
    double goldMul;
    int    bombBonus;
    double tideInterval;
    int    richBonus;
    int    safetyBonus;

    UpgradeEffects()
        : speedMul(1.0), powerBonus(0.0), timeBonus(0.0),
          bombReduce(0.0), magnet(0.0), luckMul(1.0),
          critChance(0.0), goldMul(1.0), bombBonus(0),
          tideInterval(18.0), richBonus(0), safetyBonus(0) {}
};

enum EffectField {
    EF_SPEED = 0, EF_POWER, EF_TIME, EF_BOMB_RED, EF_MAGNET, EF_LUCK,
    EF_CRIT, EF_GOLD, EF_BOMB_BONUS, EF_TIDE, EF_RICH, EF_SAFETY
};

struct EffectRule {
    EffectField field;
    double base;
    double perLevel;
};

const EffectRule EFFECT_RULES[NUM_UPGRADES] = {
    {EF_SPEED,      1.0,  0.10},
    {EF_POWER,      0.0,  0.08},
    {EF_TIME,       0.0,  8.00},
    {EF_BOMB_RED,   0.0,  0.12},
    {EF_MAGNET,     0.0,  0.20},
    {EF_LUCK,       1.0,  0.10},
    {EF_CRIT,       0.0,  0.08},
    {EF_GOLD,       1.0,  0.15},
    {EF_BOMB_BONUS, 0.0, 12.00},
    {EF_TIDE,      18.0,  3.00},
    {EF_RICH,       0.0,  1.00},
    {EF_SAFETY,     0.0,  1.00},
};

UpgradeEffects computeEffects(const SaveData& d,
                              const bool disabled[NUM_UPGRADES])
{
    UpgradeEffects e;
    for (int i = 0; i < NUM_UPGRADES; i++) {
        if (disabled[i]) continue;
        int lv = clampVal(d.upLv[i], 0, UPG[i].maxLv);
        const EffectRule& r = EFFECT_RULES[i];
        double v = r.base + r.perLevel * lv;
        switch (r.field) {
            case EF_SPEED:      e.speedMul     = v; break;
            case EF_POWER:      e.powerBonus   = v; break;
            case EF_TIME:       e.timeBonus    = v; break;
            case EF_BOMB_RED:   e.bombReduce   = v; break;
            case EF_MAGNET:     e.magnet       = v; break;
            case EF_LUCK:       e.luckMul      = v; break;
            case EF_CRIT:       e.critChance   = v; break;
            case EF_GOLD:       e.goldMul      = v; break;
            case EF_BOMB_BONUS: e.bombBonus    = (int)v; break;
            case EF_TIDE:       e.tideInterval = v; break;
            case EF_RICH:       e.richBonus    = (int)v; break;
            case EF_SAFETY:     e.safetyBonus  = (int)v; break;
        }
    }
    if (e.bombReduce > BOMB_REDUCE_CAP) e.bombReduce = BOMB_REDUCE_CAP;
    if (e.bombBonus  > BOMB_BONUS_CAP)  e.bombBonus  = BOMB_BONUS_CAP;
    return e;
}

// ================== 游戏状态 ==================
struct GameState {
    SaveData       save;
    UpgradeEffects eff;

    int    level;
    int    score;
    int    target;
    double timeLeft;

    bool   endless;
    int    endlessWave;

    int    minerX;
    int    minerY;
    int    minerFace;
    double angle;
    double hookLen;
    double maxHookLen;
    int    swingDir;
    int    state;
    Item*  grabbed;
    bool   paused;

    vector<Item> items;
    deque<MsgLine> msgLines;
    vector<FloatingText> floats;

    int    levelGoldCatch;
    int    levelBombCatch;
    int    levelRockCatch;
    int    levelDiamondCatch;
    int    levelMouseCatch;
    int    levelCrabCatch;
    int    noBombStreak;
    int    totalCrit;
    double tideTimer;

    double shakeMag;

    int    comboCount;
    int    bestCombo;
    int    missStreak;
    int    hitStreak;

    int    levelCritCount;
    int    levelMissCount;
    int    levelTideCount;
    double levelStartTime;

    vector<BuffId> activeBuffs;

    bool disabledUpg[NUM_UPGRADES];
    vector<string> achToasts;

    vector<vector<pair<char,int> > > grid;

    GameState()
        : level(1), score(0), target(0), timeLeft(0),
          endless(false), endlessWave(0),
          minerX(W/2), minerY(1), minerFace(0),
          angle(0), hookLen(HOOK_MIN_LEN), maxHookLen(0),
          swingDir(1), state(0), grabbed(NULL), paused(false),
          levelGoldCatch(0), levelBombCatch(0), levelRockCatch(0),
          levelDiamondCatch(0), levelMouseCatch(0), levelCrabCatch(0),
          noBombStreak(0), totalCrit(0), tideTimer(0),
          shakeMag(0), comboCount(0), bestCombo(0),
          missStreak(0), hitStreak(0),
          levelCritCount(0), levelMissCount(0), levelTideCount(0),
          levelStartTime(0)
    {
        grid.assign(H, vector<pair<char,int> >(W, make_pair((char)' ', 0)));
        for (int i = 0; i < NUM_UPGRADES; i++) disabledUpg[i] = false;
    }

    void applyEffects() {
        eff = computeEffects(save, disabledUpg);
        applyBuffsToEffects();
    }

    bool hasBuff(BuffId id) const {
        for (size_t i = 0; i < activeBuffs.size(); i++)
            if (activeBuffs[i] == id) return true;
        return false;
    }

    void applyBuffsToEffects() {
        for (size_t i = 0; i < activeBuffs.size(); i++) {
            switch (activeBuffs[i]) {
                case BUFF_SPEED_UP:    eff.speedMul   *= 1.25; break;
                case BUFF_POWER_UP:
                    eff.powerBonus = min(0.8, eff.powerBonus + 0.4); break;
                case BUFF_TIME_UP:     eff.timeBonus  += 15;   break;
                case BUFF_CRIT_UP:     eff.critChance += 0.20; break;
                case BUFF_GOLD_UP:     eff.goldMul    *= 1.30; break;
                case BUFF_MAGNET_UP:   eff.magnet     += 0.5;  break;
                case BUFF_BOMB_SAFE:
                    eff.bombReduce = min(0.9, eff.bombReduce + 0.5); break;
                case BUFF_NO_TIDE:     eff.tideInterval = 9999; break;
                case BUFF_EXTRA_TIME:  eff.timeBonus  += 30;   break;
                default: break;
            }
        }
    }

    void syncBuffsToSave() {
        int n = (int)activeBuffs.size();
        if (n > MAX_BUFFS) n = MAX_BUFFS;
        save.endlessBuffCount = (uint8_t)n;
        for (int i = 0; i < n; i++)
            save.endlessBuffs[i] = (uint8_t)activeBuffs[i];
    }

    void loadBuffsFromSave() {
        activeBuffs.clear();
        for (int i = 0; i < save.endlessBuffCount; i++) {
            uint8_t v = save.endlessBuffs[i];
            if (v < (uint8_t)BUFF_COUNT)
                activeBuffs.push_back((BuffId)v);
        }
    }
};

// ================== 消息栏 ==================
void pushMsg(GameState& g, const string& text, int color) {
    if (!g.msgLines.empty()) {
        MsgLine& top = g.msgLines.front();
        if (top.text == text && top.color == color &&
            top.life > MSG_LIFE - 1.5) {
            top.count++;
            top.life = MSG_LIFE;
            return;
        }
    }
    MsgLine m;
    m.text  = text;
    m.color = color;
    m.life  = MSG_LIFE;
    m.count = 1;
    g.msgLines.push_front(m);
    while ((int)g.msgLines.size() > MSG_MAX) g.msgLines.pop_back();
}

void pushFloat(GameState& g, double x, double y,
               const string& text, int color) {
    FloatingText f;
    f.x = x; f.y = y;
    f.text = text; f.color = color;
    g.floats.push_back(f);
    if (g.floats.size() > 40) g.floats.erase(g.floats.begin());
}

void updateMsgs(GameState& g, double dt) {
    for (size_t i = 0; i < g.msgLines.size(); i++)
        g.msgLines[i].life -= dt;
    while (!g.msgLines.empty() && g.msgLines.back().life <= 0)
        g.msgLines.pop_back();
}

void updateFloats(GameState& g, double dt) {
    for (size_t i = 0; i < g.floats.size(); i++) {
        g.floats[i].y += g.floats[i].vy * dt;
        g.floats[i].life -= dt;
    }
    vector<FloatingText> keep;
    for (size_t i = 0; i < g.floats.size(); i++)
        if (g.floats[i].life > 0) keep.push_back(g.floats[i]);
    g.floats.swap(keep);
}

int msgDisplayColor(const MsgLine& m) {
    return (m.life < 1.5) ? FG_GRAY : m.color;
}

string msgFullText(const MsgLine& m) {
    if (m.count <= 1) return m.text;
    ostringstream oss;
    oss << m.text << " x" << m.count;
    return oss.str();
}

string truncByWidth(const string& s, int width) {
    int w = 0;
    string out;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        int bytes = 1, cw = 1;
        if (c >= 0xF0)      { bytes = 4; cw = 2; }
        else if (c >= 0xE0) { bytes = 3; cw = 2; }
        else if (c >= 0xC0) { bytes = 2; cw = 2; }
        if (w + cw > width) break;
        out.append(s, i, bytes);
        i += bytes;
        w += cw;
    }
    return out;
}
int displayWidth(const string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0xF0)      { i += 4; w += 2; }
        else if (c >= 0xE0) { i += 3; w += 2; }
        else if (c >= 0xC0) { i += 2; w += 2; }
        else                { i += 1; w += 1; }
    }
    return w;
}

// 统一消息框一行（修复缩进）
void drawMsgBoxRow(ostringstream& o, const string& text, int color) {
    o << col(FG_LMAGENTA) << "|";
    if (text.empty()) {
        for (int k = 0; k < MSG_WIDTH + 2; k++) o << " ";
    } else {
        int w = displayWidth(text);
        o << col(color) << " " << text;
        for (int k = w; k < MSG_WIDTH + 1; k++) o << " ";
    }
    o << col(FG_LMAGENTA) << "|";
}

// ================== 成就 ==================
void unlockAch(GameState& g, int idx) {
    if (idx < 0 || idx >= A_NUM_ACH) return;
    if (g.save.unlocked[idx]) return;
    g.save.unlocked[idx] = true;
    g.achToasts.push_back(string("ACH: ") + ACHS[idx].name +
                          " - " + ACHS[idx].desc);
    sndAch();
}

// ================== 抓取回调 ==================
void onGrabItem(GameState& g, char ch, bool isCrit) {
    if (ch == '*') {
        g.save.stats.diamondCount++;
        g.levelDiamondCatch++;
        unlockAch(g, A_DIAMOND_HUNTER);
        if (g.save.stats.diamondCount >= 5) unlockAch(g, A_DIAMOND_HOARD);
        if (g.levelDiamondCatch >= 5)       unlockAch(g, A_5_DIAMONDS_ONE);
    }
    if (ch == 'C') {
        g.save.stats.crabCount++;
        g.levelCrabCatch++;
        if (g.save.stats.crabCount >= 3) unlockAch(g, A_CRAB_HUNTER);
    }
    if (ch == 'r') {
        g.save.stats.mouseCount++;
        g.levelMouseCatch++;
        if (g.save.stats.mouseCount >= 10) unlockAch(g, A_MOUSE_HUNTER);
    }
    if (ch == '#') g.levelRockCatch++;

    if (ch == '@' || ch == 'O' || ch == 'o') {
        g.levelGoldCatch++;
        if (g.levelGoldCatch >= 5)  unlockAch(g, A_GOLD_RUSH);
        if (g.levelGoldCatch >= 10) unlockAch(g, A_10_GOLDS_ONE);
    }

    g.noBombStreak++;
    if (g.noBombStreak >= 10) unlockAch(g, A_STEADY_HAND);

    if (isCrit) {
        g.totalCrit++;
        g.levelCritCount++;
        if (g.totalCrit >= 10) unlockAch(g, A_LUCKY_STRIKE);
    }
    if (g.save.gold >= 1000)  unlockAch(g, A_RISING_STAR);
    if (g.save.gold >= 10000) unlockAch(g, A_GOLD_BARON);

    g.comboCount++;
    if (g.comboCount > g.bestCombo) g.bestCombo = g.comboCount;
    g.hitStreak++;
    g.missStreak = 0;
    if (g.comboCount >= 10) unlockAch(g, A_COMBO_10);
    if (g.comboCount >= 20) unlockAch(g, A_COMBO_20);
    if (g.hitStreak >= 5)   unlockAch(g, A_NO_MISS_5);
}

void onGrabBomb(GameState& g) {
    g.noBombStreak = 0;
    g.levelBombCatch++;
    g.comboCount   = 0;
    g.hitStreak    = 0;
}

void onMissHook(GameState& g) {
    g.comboCount = 0;
    g.hitStreak  = 0;
    g.missStreak++;
    g.levelMissCount++;
}

// ================== 关卡生成 ==================
namespace levelgen {

bool tryPlace(GameState& g, int value, double weight, char ch, int color,
              MoveType mv, bool pulse,
              int minX, int maxX, int minY, int maxY,
              int minGapX, int minGapY)
{
    for (int tries = 0; tries < 400; tries++) {
        int x = randInt(minX, maxX);
        int y = randInt(minY, maxY);
        bool ok = true;
        for (size_t i = 0; i < g.items.size(); i++) {
            if (abs((int)g.items[i].x - x) <= minGapX &&
                abs((int)g.items[i].y - y) <= minGapY) {
                ok = false; break;
            }
        }
        if (abs(x - g.minerX) <= 1 && abs(y - g.minerY) <= 2) ok = false;
        if (!ok) continue;

        Item it;
        it.x = x; it.y = y;
        it.value = value; it.weight = weight;
        it.ch = ch; it.color = color; it.alive = true;
        it.pulse = pulse;
        it.mvType = mv;
        if (mv == MV_CRAB)  it.mvDir = randInt(0, 1) ? 1 : -1;
        if (mv == MV_MOUSE) {
            it.mvDir = randInt(0, 3);
            it.mvTimer = randDouble(0.5, 1.2);
        }
        g.items.push_back(it);
        return true;
    }
    return false;
}

int place(GameState& g, int cnt, int value, double weight, char ch, int color,
          MoveType mv, bool pulse)
{
    const int minY = 4, maxY = H - 2;
    const int minX = 1, maxX = W - 2;
    int placed = 0;
    for (int i = 0; i < cnt; i++) {
        if (tryPlace(g, value, weight, ch, color, mv, pulse,
                     minX, maxX, minY, maxY, 2, 1)) { placed++; continue; }
        if (tryPlace(g, value, weight, ch, color, mv, pulse,
                     minX, maxX, minY, maxY, 1, 1)) { placed++; continue; }
    }
    return placed;
}

void spawnStaticItems(GameState& g, int lv) {
    int extra = min(3, (lv - 1) / 2);

    const ItemDef& dBig = ITEM_DEFS[IK_BIG_GOLD];
    place(g, dBig.spawnBase + randInt(0, dBig.spawnRand),
          dBig.value, dBig.weight, dBig.ch, dBig.color,
          dBig.move, dBig.pulse);

    const ItemDef& dMid = ITEM_DEFS[IK_MID_GOLD];
    place(g, dMid.spawnBase + randInt(0, dMid.spawnRand) + extra,
          dMid.value, dMid.weight, dMid.ch, dMid.color,
          dMid.move, dMid.pulse);

    const ItemDef& dSm = ITEM_DEFS[IK_SMALL_GOLD];
    place(g, dSm.spawnBase + randInt(0, dSm.spawnRand) + extra + g.eff.richBonus,
          dSm.value, dSm.weight, dSm.ch, dSm.color,
          dSm.move, dSm.pulse);

    int diaCnt = ITEM_DEFS[IK_DIAMOND].spawnBase
               + randInt(0, ITEM_DEFS[IK_DIAMOND].spawnRand);
    if (g.endless && g.hasBuff(BUFF_EXTRA_DIAMOND)) diaCnt += 2;
    place(g, diaCnt,
          ITEM_DEFS[IK_DIAMOND].value, ITEM_DEFS[IK_DIAMOND].weight,
          ITEM_DEFS[IK_DIAMOND].ch, ITEM_DEFS[IK_DIAMOND].color,
          ITEM_DEFS[IK_DIAMOND].move, ITEM_DEFS[IK_DIAMOND].pulse);

    const ItemDef& dRock = ITEM_DEFS[IK_ROCK];
    place(g, dRock.spawnBase + randInt(0, dRock.spawnRand),
          dRock.value, dRock.weight, dRock.ch, dRock.color,
          dRock.move, dRock.pulse);

    int bombCnt = ITEM_DEFS[IK_BOMB].spawnBase
                + randInt(0, ITEM_DEFS[IK_BOMB].spawnRand)
                - g.eff.safetyBonus;
    if (bombCnt < 0) bombCnt = 0;
    place(g, bombCnt, ITEM_DEFS[IK_BOMB].value, ITEM_DEFS[IK_BOMB].weight,
          ITEM_DEFS[IK_BOMB].ch, ITEM_DEFS[IK_BOMB].color,
          ITEM_DEFS[IK_BOMB].move, ITEM_DEFS[IK_BOMB].pulse);
}

void spawnCrabs(GameState& g, int lv) {
    if (lv < 2) return;
    const ItemDef& d = ITEM_DEFS[IK_CRAB];
    int cnt = d.spawnBase + randInt(0, d.spawnRand);
    for (int i = 0; i < cnt; i++) {
        for (int tries = 0; tries < 200; tries++) {
            int x = randInt(3, W - 4);
            int y = randInt(6, H - 2);
            bool ok = true;
            for (size_t j = 0; j < g.items.size(); j++) {
                if (abs((int)g.items[j].x - x) <= 2 &&
                    abs((int)g.items[j].y - y) <= 1) {
                    ok = false; break;
                }
            }
            if (!ok) continue;
            Item it;
            it.x = x; it.y = y;
            it.value = d.value; it.weight = d.weight;
            it.ch = d.ch; it.color = d.color;
            it.pulse = d.pulse;
            it.mvType = MV_CRAB;
            it.mvDir = randInt(0, 1) ? 1 : -1;
            g.items.push_back(it);
            break;
        }
    }
}

void spawnMice(GameState& g, int lv) {
    if (lv < 3) return;
    const ItemDef& d = ITEM_DEFS[IK_MOUSE];
    int cnt = d.spawnBase + randInt(0, d.spawnRand);
    for (int i = 0; i < cnt; i++) {
        for (int tries = 0; tries < 200; tries++) {
            int x = randInt(3, W - 4);
            int y = randInt(7, H - 2);
            bool ok = true;
            for (size_t j = 0; j < g.items.size(); j++) {
                if (abs((int)g.items[j].x - x) <= 2 &&
                    abs((int)g.items[j].y - y) <= 1) {
                    ok = false; break;
                }
            }
            if (!ok) continue;
            Item it;
            it.x = x; it.y = y;
            it.value = d.value; it.weight = d.weight;
            it.ch = d.ch; it.color = d.color;
            it.pulse = d.pulse;
            it.mvType = MV_MOUSE;
            it.mvDir = randInt(0, 3);
            it.mvTimer = randDouble(0.5, 1.2);
            g.items.push_back(it);
            break;
        }
    }
}

void rollDisabledUpgrades(GameState& g, int lv) {
    for (int i = 0; i < NUM_UPGRADES; i++) g.disabledUpg[i] = false;
    if (lv < 3) return;
    int cnt = randInt(1, 2);
    while (cnt > 0) {
        int idx = randInt(0, NUM_UPGRADES - 1);
        if (!g.disabledUpg[idx]) { g.disabledUpg[idx] = true; cnt--; }
    }
}

} // namespace levelgen

void resetLevelStats(GameState& g) {
    g.levelGoldCatch = 0;
    g.levelBombCatch = 0;
    g.levelRockCatch = 0;
    g.levelDiamondCatch = 0;
    g.levelMouseCatch = 0;
    g.levelCrabCatch = 0;
    g.noBombStreak   = 0;
    g.tideTimer      = 0;
    g.comboCount     = 0;
    g.bestCombo      = 0;
    g.missStreak     = 0;
    g.hitStreak      = 0;
    g.shakeMag       = 0;
    g.levelCritCount = 0;
    g.levelMissCount = 0;
    g.levelTideCount = 0;
    g.levelStartTime = nowSeconds();
}

void initLevel(GameState& g, int lv) {
    using namespace levelgen;

    g.items.clear();
    g.msgLines.clear();
    g.floats.clear();
    g.grabbed = NULL;
    g.score = 0;
    g.level = lv;
    g.target = 300 + (lv - 1) * 250;
    g.endless = false;
    g.endlessWave = 0;

    resetLevelStats(g);
    rollDisabledUpgrades(g, lv);
    g.applyEffects();

    g.timeLeft   = 60.0 + g.eff.timeBonus;
    g.angle      = 0;
    g.hookLen    = HOOK_MIN_LEN;
    g.swingDir   = 1;
    g.state      = 0;
    g.paused     = false;
    g.minerFace  = 0;
    g.minerX     = W / 2;
    g.minerY     = 1;
    g.maxHookLen = sqrt((double)(W - 1) * (W - 1) +
                        (double)(H - 1) * (H - 1)) + 2.0;

    spawnStaticItems(g, lv);
    spawnCrabs(g, lv);
    spawnMice(g, lv);

    pushMsg(g, "Level " + to_string(lv) + " start!", FG_LCYAN);
    if (lv >= 2) pushMsg(g, "Crabs are moving!", FG_LRED);
    if (lv >= 3) pushMsg(g, "Mice are sneaking...", FG_GRAY);
}

void initEndlessLevel(GameState& g, int wave) {
    using namespace levelgen;

    g.items.clear();
    g.msgLines.clear();
    g.floats.clear();
    g.grabbed = NULL;
    g.score = 0;
    g.endless     = true;
    g.endlessWave = wave;
    g.level       = wave;

    g.target = (int)(400 * pow(1.18, wave - 1));
    if (g.target < 400) g.target = 400;

    resetLevelStats(g);

    for (int i = 0; i < NUM_UPGRADES; i++) g.disabledUpg[i] = false;
    int disableCnt = (wave >= 3) ? min(4, 1 + (wave - 3) / 2) : 0;
    while (disableCnt > 0) {
        int idx = randInt(0, NUM_UPGRADES - 1);
        if (!g.disabledUpg[idx]) { g.disabledUpg[idx] = true; disableCnt--; }
    }
    g.applyEffects();

    double baseTime = 60.0 - wave * 1.5;
    if (baseTime < 30.0) baseTime = 30.0;
    g.timeLeft = baseTime + g.eff.timeBonus;

    g.angle      = 0;
    g.hookLen    = HOOK_MIN_LEN;
    g.swingDir   = 1;
    g.state      = 0;
    g.paused     = false;
    g.minerFace  = 0;
    g.minerX     = W / 2;
    g.minerY     = 1;
    g.maxHookLen = sqrt((double)(W - 1) * (W - 1) +
                        (double)(H - 1) * (H - 1)) + 2.0;

    int extra = min(3, wave / 3);
    const ItemDef& dBig = ITEM_DEFS[IK_BIG_GOLD];
    place(g, dBig.spawnBase + randInt(0, dBig.spawnRand),
          dBig.value, dBig.weight, dBig.ch, dBig.color,
          dBig.move, dBig.pulse);
    const ItemDef& dMid = ITEM_DEFS[IK_MID_GOLD];
    place(g, dMid.spawnBase + randInt(0, dMid.spawnRand) + extra,
          dMid.value, dMid.weight, dMid.ch, dMid.color,
          dMid.move, dMid.pulse);
    const ItemDef& dSm = ITEM_DEFS[IK_SMALL_GOLD];
    place(g, dSm.spawnBase + randInt(0, dSm.spawnRand) + extra + g.eff.richBonus,
          dSm.value, dSm.weight, dSm.ch, dSm.color,
          dSm.move, dSm.pulse);

    int diaCnt = ITEM_DEFS[IK_DIAMOND].spawnBase
               + randInt(0, ITEM_DEFS[IK_DIAMOND].spawnRand);
    if (g.hasBuff(BUFF_EXTRA_DIAMOND)) diaCnt += 2;
    place(g, diaCnt,
          ITEM_DEFS[IK_DIAMOND].value, ITEM_DEFS[IK_DIAMOND].weight,
          ITEM_DEFS[IK_DIAMOND].ch, ITEM_DEFS[IK_DIAMOND].color,
          ITEM_DEFS[IK_DIAMOND].move, ITEM_DEFS[IK_DIAMOND].pulse);

    const ItemDef& dRock = ITEM_DEFS[IK_ROCK];
    place(g, dRock.spawnBase + randInt(0, dRock.spawnRand),
          dRock.value, dRock.weight, dRock.ch, dRock.color,
          dRock.move, dRock.pulse);

    int bombCnt = 1 + randInt(0, 1) + wave / 2 - g.eff.safetyBonus;
    if (bombCnt < 0) bombCnt = 0;
    place(g, bombCnt, ITEM_DEFS[IK_BOMB].value, ITEM_DEFS[IK_BOMB].weight,
          ITEM_DEFS[IK_BOMB].ch, ITEM_DEFS[IK_BOMB].color,
          ITEM_DEFS[IK_BOMB].move, ITEM_DEFS[IK_BOMB].pulse);

    spawnCrabs(g, 2);
    spawnMice(g, 3);

    pushMsg(g, "ENDLESS Wave " + to_string(wave), FG_LMAGENTA);
    if (wave % 3 == 0) pushMsg(g, "More bombs incoming!", FG_LRED);
}

// ================== 物品移动 ==================
void updateMovingItems(GameState& g, double dt) {
    for (size_t i = 0; i < g.items.size(); i++) {
        Item& it = g.items[i];
        if (!it.alive || it.mvType == MV_STATIC) continue;
        if (it.grabbedFlag) continue;

        if (it.mvType == MV_CRAB) {
            it.x += it.mvDir * 2.0 * dt;
            if (it.x < 2)     { it.x = 2;     it.mvDir =  1; }
            if (it.x > W - 3) { it.x = W - 3; it.mvDir = -1; }
        } else if (it.mvType == MV_MOUSE) {
            it.mvTimer -= dt;
            if (it.mvTimer <= 0) {
                it.mvTimer = randDouble(0.4, 1.2);
                it.mvDir = randInt(0, 3);
            }
            static const int dx[4] = {-1, 1, 0, 0};
            static const int dy[4] = {0, 0, -1, 1};
            it.x += dx[it.mvDir] * 3.0 * dt;
            it.y += dy[it.mvDir] * 3.0 * dt;

            if (it.x < 2)     { it.x = 2;     it.mvDir = 1; }
            if (it.x > W - 3) { it.x = W - 3; it.mvDir = 0; }
            if (it.y < 4)     { it.y = 4;     it.mvDir = 3; }
            if (it.y > H - 2) { it.y = H - 2; it.mvDir = 2; }
        }
    }
}

// ================== 钩子采样 ==================
Item* sweepHook(GameState& g, double oldLen, double newLen) {
    double delta = newLen - oldLen;
    if (delta <= 0) return NULL;
    int steps = (int)ceil(delta / SUBSTEP_MIN);
    if (steps < 1) steps = 1;
    for (int s = 1; s <= steps; s++) {
        double t = (double)s / steps;
        double testLen = oldLen + delta * t;
        double tx = g.minerX + testLen * sin(g.angle);
        double ty = g.minerY + testLen * cos(g.angle);

        for (size_t i = 0; i < g.items.size(); i++) {
            Item& item = g.items[i];
            if (!item.alive) continue;
            double d = hypot(tx - item.x, ty - item.y);
            if (d < GRAB_RADIUS + g.eff.magnet) return &item;
        }
    }
    return NULL;
}

// ================== 状态机 ==================
void updateSwinging(GameState& g, double dt) {
    g.minerFace = 0;
    g.angle += g.swingDir * SWING_SPEED * dt;
    if (g.angle >  SWING_MAX_ANGLE) { g.angle =  SWING_MAX_ANGLE; g.swingDir = -1; }
    if (g.angle < -SWING_MAX_ANGLE) { g.angle = -SWING_MAX_ANGLE; g.swingDir =  1; }
}

void updateExtending(GameState& g, double dt) {
    g.minerFace = 1;
    double oldLen = g.hookLen;
    g.hookLen += HOOK_EXTEND_SPEED * dt;

    Item* hit = sweepHook(g, oldLen, g.hookLen);
    if (hit) {
        hit->alive = false;
        hit->grabbedFlag = true;
        g.grabbed = hit;
        g.state = 2;
        return;
    }

    double hx = g.minerX + g.hookLen * sin(g.angle);
    double hy = g.minerY + g.hookLen * cos(g.angle);
    if (hx < 0 || hx >= W || hy < 0 || hy >= H ||
        g.hookLen > g.maxHookLen)
        g.state = 2;
}

void resolveGrab(GameState& g) {
    Item* it = g.grabbed;
    if (!it) return;

    int rawVal = it->value;
    char gch = it->ch;
    const ItemDef& def = defOf(gch);
    string name = def.name;

    if (rawVal > 0) {
        bool isCrit = chance(g.eff.critChance);
        if (gch == '*') isCrit = false;
        double val = rawVal * g.eff.luckMul * (isCrit ? 1.5 : 1.0);
        int finalVal = (int)llround(val);

        if (gch == '*' && g.endless && g.hasBuff(BUFF_DOUBLE_DIAMOND))
            finalVal *= 2;

        g.score += finalVal;
        int goldGain = (int)llround(finalVal * g.eff.goldMul);
        g.save.gold += goldGain;
        g.save.stats.totalGrabbed++;

        if (isCrit) {
            sndCrit();
            pushMsg(g, "CRIT +" + to_string(finalVal) + " " + name, FG_LMAGENTA);
            pushFloat(g, it->x, it->y, "+" + to_string(finalVal), FG_LMAGENTA);
            if (g.shakeMag < 1.0) g.shakeMag = 1.0;
        } else {
            sndEat();
            pushMsg(g, "+" + to_string(finalVal) + " " + name, FG_LYELLOW);
            pushFloat(g, it->x, it->y, "+" + to_string(finalVal), FG_LYELLOW);
        }
        onGrabItem(g, gch, isCrit);
    } else {
        if (g.endless && g.hasBuff(BUFF_REFLECT)) {
            int finalVal = 50;
            g.score += finalVal;
            sndEat();
            pushMsg(g, "+50 Bomb (Reflect)", FG_LGREEN);
            pushFloat(g, it->x, it->y, "+50", FG_LGREEN);
            it->grabbedFlag = false;
            g.grabbed = NULL;
            return;
        }

        int rawDmg = -rawVal;
        double reduced = rawDmg * (1.0 - g.eff.bombReduce);
        int dmg = (int)llround(reduced) - g.eff.bombBonus;
        if (dmg < BOMB_DAMAGE_FLOOR) dmg = BOMB_DAMAGE_FLOOR;
        int finalVal = -dmg;
        g.score += finalVal;
        sndBomb();
        pushMsg(g, to_string(finalVal) + " " + name, FG_LRED);
        pushFloat(g, it->x, it->y, to_string(finalVal), FG_LRED);
        if (g.shakeMag < 3.0) g.shakeMag = 3.0;
        onGrabBomb(g);
    }
    it->grabbedFlag = false;
    g.grabbed = NULL;
}

void updateRetracting(GameState& g, double dt) {
    g.minerFace = 2;
    double spd = HOOK_RETRACT_SPEED * g.eff.speedMul;
    if (g.grabbed) {
        double effW = g.grabbed->weight * (1.0 - g.eff.powerBonus);
        if (effW < 0.5) effW = 0.5;
        spd = spd / (1.0 + effW * 0.6);
    }
    g.hookLen -= spd * dt;
    if (g.hookLen < HOOK_MIN_LEN) g.hookLen = HOOK_MIN_LEN;

    if (g.grabbed) {
        double hx = g.minerX + g.hookLen * sin(g.angle);
        double hy = g.minerY + g.hookLen * cos(g.angle);
        g.grabbed->x = hx;
        g.grabbed->y = hy;
    }

    if (g.hookLen <= HOOK_MIN_LEN) {
        g.hookLen = HOOK_MIN_LEN;
        if (g.grabbed) resolveGrab(g);
        else            onMissHook(g);
        g.state = 0;
    }
}

void applyTide(GameState& g) {
    sndTide();
    g.levelTideCount++;
    pushMsg(g, "~ TIDE RISES ~", FG_LBLUE);
    for (size_t i = 0; i < g.items.size(); i++) {
        Item& it = g.items[i];
        if (!it.alive) continue;
        if (it.grabbedFlag) continue;
        it.y += 1.0;
        if (it.y >= H - 2) {
            if (it.y >= H - 1.5) it.alive = false;
            else                 it.y = H - 3;
        }
    }
}

void update(GameState& g, double dt) {
    updateMsgs(g, dt);
    updateFloats(g, dt);
    updateMovingItems(g, dt);

    g.shakeMag -= 30.0 * dt;
    if (g.shakeMag < 0) g.shakeMag = 0;

    g.tideTimer += dt;
    if (g.tideTimer >= g.eff.tideInterval) {
        g.tideTimer = 0;
        applyTide(g);
    }

    switch (g.state) {
        case 0: updateSwinging(g, dt);   break;
        case 1: updateExtending(g, dt);  break;
        case 2: updateRetracting(g, dt); break;
        default: g.state = 0; break;
    }
    g.timeLeft -= dt;
}

// ================== 绘制 ==================
int pulseColor(const Item& it, double t) {
    if (!it.pulse) return it.color;
    double p = 0.5 + 0.5 * sin(t * 6.0);
    if (p > 0.66) return FG_LWHITE;
    if (p > 0.33) return FG_LYELLOW;
    return it.color;
}

void draw(GameState& g) {
    double t = nowSeconds();

    for (int i = 0; i < H; i++)
        for (int j = 0; j < W; j++)
            g.grid[i][j] = make_pair((char)' ', 0);

    int shakeX = 0, shakeY = 0;
    if (g.shakeMag > 0.2) {
        shakeX = randInt(-(int)g.shakeMag, (int)g.shakeMag);
        shakeY = randInt(-(int)g.shakeMag, (int)g.shakeMag);
    }

    // C++11 兼容：使用 std::function 或宏
    #define PLOT(x, y, ch, c) do { \
        int _xx = (x) + shakeX, _yy = (y) + shakeY; \
        if (_xx >= 0 && _xx < W && _yy >= 0 && _yy < H) \
            g.grid[_yy][_xx] = make_pair((char)(ch), (int)(c)); \
    } while(0)

    for (size_t i = 0; i < g.items.size(); i++) {
        Item& item = g.items[i];
        if (!item.alive) continue;
        int x = (int)round(item.x), y = (int)round(item.y);
        PLOT(x, y, item.ch, pulseColor(item, t));
    }

    double hx = g.minerX + g.hookLen * sin(g.angle);
    double hy = g.minerY + g.hookLen * cos(g.angle);
    int steps = (int)(g.hookLen * 3) + 1;
    for (int i = 0; i <= steps; i++) {
        double tt = (double)i / steps;
        int x = (int)round(g.minerX + tt * (hx - g.minerX));
        int y = (int)round(g.minerY + tt * (hy - g.minerY));
        if (i == steps) PLOT(x, y, 'v', FG_LYELLOW);
        else            PLOT(x, y, '.', FG_YELLOW);
    }

    char face = 'M';
    int  faceColor = FG_LWHITE;
    if (g.paused)          { face = 'Z'; faceColor = FG_LYELLOW; }
    else if (g.state == 1) { face = 'V'; faceColor = FG_LGREEN; }
    else if (g.state == 2) { face = 'A'; faceColor = FG_LCYAN; }
    PLOT(g.minerX, g.minerY, face, faceColor);

    if (g.grabbed) {
        int x = (int)round(hx), y = (int)round(hy);
        PLOT(x, y, g.grabbed->ch, g.grabbed->color);
    }

    #undef PLOT

    ostringstream o;
    o << "\033[H";

    string timeStr = to_string((int)ceil(g.timeLeft)) + "s";
    if (g.timeLeft < TIME_WARN) timeStr = "[!! " + timeStr + " !!]";

    if (g.endless) {
        o << col(FG_LMAGENTA) << "  Endless W" << g.endlessWave
          << col(FG_LYELLOW)  << "  " << g.score << "/" << g.target
          << col(FG_LRED)     << "  " << timeStr
          << col(FG_LGREEN)   << "  " << g.save.gold << "G";
    } else {
        o << col(FG_LCYAN)    << "  Lv." << g.level
          << col(FG_LYELLOW)  << "  " << g.score << "/" << g.target
          << col(FG_LRED)     << "  " << timeStr
          << col(FG_LGREEN)   << "  " << g.save.gold << "G";
    }

    if (g.comboCount >= 2)
        o << col(FG_LMAGENTA) << "  x" << g.comboCount;

    o << col(g.save.soundOn ? FG_LGREEN : FG_GRAY)
      << (g.save.soundOn ? "  \u266a" : "  x");
    o << "    ";
    o << col(FG_LMAGENTA) << "Recent" << CLR_LINE << "\n";

    {
        int barW = 30;
        int filled = (g.target > 0)
            ? (int)((double)g.score / g.target * barW) : 0;
        filled = clampVal(filled, 0, barW);
        o << "  " << col(FG_GREEN) << "[";
        for (int i = 0; i < barW; i++) {
            if (i < filled) o << (i < barW / 2 ? '=' : '#');
            else            o << '-';
        }
        o << "]";
        double pct = (g.target > 0)
            ? (double)g.score / g.target * 100.0 : 0.0;
        o << "  " << col(pct >= 100.0 ? FG_LGREEN : FG_GRAY)
          << fixed << setprecision(0) << pct << "%";
        o << CLR_LINE << "\n";
    }

    o << col(FG_CYAN) << "  +";
    for (int i = 0; i < W; i++) o << "-";
    o << "+   ";
    o << col(FG_LMAGENTA) << "+";
    for (int i = 0; i < MSG_WIDTH + 2; i++) o << "-";
    o << "+" << CLR_LINE << "\n";

    for (int i = 0; i < H; i++) {
        o << col(FG_CYAN) << "  |";
        for (int j = 0; j < W; j++) {
            char ch = g.grid[i][j].first;
            int  c  = g.grid[i][j].second;
            if (ch == ' ') o << " ";
            else           o << col(c) << ch << col(FG_CYAN);
        }
        o << "|   ";

        if (i < (int)g.msgLines.size()) {
            int c = msgDisplayColor(g.msgLines[i]);
            string s = truncByWidth(msgFullText(g.msgLines[i]), MSG_WIDTH);
            drawMsgBoxRow(o, s, c);
        } else {
            drawMsgBoxRow(o, "", 0);
        }
        o << CLR_LINE << "\n";
    }

    o << col(FG_CYAN) << "  +";
    for (int i = 0; i < W; i++) o << "-";
    o << "+   ";
    o << col(FG_LMAGENTA) << "+";
    for (int i = 0; i < MSG_WIDTH + 2; i++) o << "-";
    o << "+" << CLR_LINE << "\n";

    if (g.paused) {
        o << col(FG_LYELLOW) << "  === PAUSED ===" << CLR_LINE << "\n";
        o << col(FG_GRAY)
          << "  Speed x" << fixed << setprecision(2) << g.eff.speedMul
          << " | Wt -" << (int)(g.eff.powerBonus * 100) << "%"
          << " | +" << (int)g.eff.timeBonus << "s"
          << " | Bomb -" << (int)(g.eff.bombReduce * 100) << "%"
          << CLR_LINE << "\n";
        o << col(FG_GRAY)
          << "  Mag +" << setprecision(1) << g.eff.magnet
          << " | Luck x" << setprecision(2) << g.eff.luckMul
          << " | Crit " << (int)(g.eff.critChance * 100) << "%"
          << " | Gold x" << g.eff.goldMul
          << " | Bomb+" << g.eff.bombBonus
          << CLR_LINE << "\n";

        if (g.endless && !g.activeBuffs.empty()) {
            o << col(FG_LMAGENTA) << "  Buffs: ";
            for (size_t i = 0; i < g.activeBuffs.size(); i++) {
                for (int j = 0; j < NUM_BUFFS; j++) {
                    if (BUFFS[j].id == g.activeBuffs[i]) {
                        o << "[" << BUFFS[j].name << "] ";
                        break;
                    }
                }
            }
            o << CLR_LINE << "\n";
        } else {
            bool any = false;
            o << col(FG_LMAGENTA) << "  Disabled: ";
            for (int i = 0; i < NUM_UPGRADES; i++) {
                if (g.disabledUpg[i]) {
                    o << "[" << UPG[i].name << "] ";
                    any = true;
                }
            }
            if (!any) o << col(FG_GRAY) << "(none)";
            o << CLR_LINE << "\n";
        }
    } else {
        o << "  [Space] Launch  [P] Pause  [Q] Save & Quit" << CLR_LINE << "\n";
        o << CLR_LINE << "\n";
    }

    o << RESET;
    cout << o.str() << flush;
}

// ================== 辅助 ==================
bool hasValuableItem(const GameState& g) {
    for (size_t i = 0; i < g.items.size(); i++)
        if (g.items[i].alive && g.items[i].value > 0) return true;
    return false;
}

bool isLevelCleared(const GameState& g) {
    return (!hasValuableItem(g) && g.state == 0 && !g.grabbed);
}

void waitAnyKey() {
    while (!plat::kbhit()) sleepMs(20);
    (void)readKey();
}
// ================== 结算动画 ==================
void animateNumber(int target, const string& label, int color,
                   int labelWidth)
{
    for (int i = 0; i <= 18; i++) {
        int v = (int)((double)target * i / 18);
        ostringstream oss;
        oss << "\r" << CLR_LINE
            << "      " << col(color)
            << setw(labelWidth) << left << label << right
            << ": " << setw(6) << v;
        cout << oss.str() << flush;
        sleepMs(i == 0 ? 1 : 28);
    }
    ostringstream oss;
    oss << "\r" << CLR_LINE
        << "      " << col(color)
        << setw(labelWidth) << left << label << right
        << ": " << setw(6) << target;
    cout << oss.str() << flush;
    cout << "\n";  // 重要：结束换行，防止覆盖下一行
}

void animateBar(int score, int target, int barW) {
    int filled = (target > 0)
        ? (int)((double)score / target * barW) : 0;
    filled = clampVal(filled, 0, barW);
    if (filled > barW) filled = barW;

    for (int i = 0; i <= barW; i++) {
        ostringstream oss;
        oss << "\r" << CLR_LINE
            << "      " << col(FG_GREEN) << "[";
        for (int k = 0; k < barW; k++) {
            if (k < i) oss << (k < barW / 2 ? '=' : '#');
            else       oss << '-';
        }
        oss << "] " << col(FG_LYELLOW)
            << (int)((double)score * i / barW) << "/" << target;
        cout << oss.str() << flush;
        sleepMs(20);
    }
    ostringstream oss;
    oss << "\r" << CLR_LINE
        << "      " << col(FG_GREEN) << "[";
    for (int k = 0; k < barW; k++) {
        if (k < filled) oss << (k < barW / 2 ? '=' : '#');
        else            oss << '-';
    }
    oss << "] " << col(FG_LYELLOW) << score << "/" << target;
    cout << oss.str() << flush;
    cout << "\n";  // 换行
}
void flashBanner(const string& text, int color, int times) {
    for (int i = 0; i < times; i++) {
        cout << "\r      " << col(color) << text << CLR_LINE << flush;
        sleepMs(110);
        cout << "\r      " << string(text.size(), ' ') << CLR_LINE << flush;
        sleepMs(110);
    }
    cout << "\r      " << col(color) << text << CLR_LINE << flush;
    cout << "\n";
}

void animateToasts(const vector<string>& toasts) {
    if (toasts.empty()) return;
    cout << col(FG_LMAGENTA) << "      Achievements Unlocked:\n";
    for (size_t i = 0; i < toasts.size(); i++) {
        cout << "        - " << col(FG_LGREEN) << toasts[i]
             << col(FG_WHITE) << "\n" << flush;
        sndAch();
        sleepMs(220);
    }
    cout << "\n";
}

// ================== 评级系统 ==================
struct Rating {
    string stars;
    string title;
    string cn;
    int    color;
};

Rating getRating(double pct) {
    Rating r;
    if (pct < 50.0) {
        r.stars = "☆☆☆☆☆";
        r.title = "FAILED";
        r.cn    = "完全没达成，再练练吧";
        r.color = FG_RED;
    } else if (pct < 80.0) {
        r.stars = "★☆☆☆☆";
        r.title = "POOR";
        r.cn    = "差一点，下次可以更好";
        r.color = FG_LRED;
    } else if (pct < 100.0) {
        r.stars = "★★☆☆☆";
        r.title = "CLOSE";
        r.cn    = "很接近目标了！";
        r.color = FG_YELLOW;
    } else if (pct < 120.0) {
        r.stars = "★★★☆☆";
        r.title = "GOOD";
        r.cn    = "达标！稳扎稳打";
        r.color = FG_LGREEN;
    } else if (pct < 150.0) {
        r.stars = "★★★★☆";
        r.title = "GREAT";
        r.cn    = "表现优秀，超额完成";
        r.color = FG_LCYAN;
    } else if (pct < 200.0) {
        r.stars = "★★★★★";
        r.title = "EXCELLENT";
        r.cn    = "完美！几乎拿走一切";
        r.color = FG_LMAGENTA;
    } else if (pct < 300.0) {
        r.stars = "★★★★★★";
        r.title = "PERFECT";
        r.cn    = "神级表现，碾压目标";
        r.color = FG_LMAGENTA;
    } else {
        r.stars = "★★★★★★★";
        r.title = "LEGENDARY";
        r.cn    = "传奇！目标已成尘埃";
        r.color = FG_LMAGENTA;
    }
    return r;
}

int pctColor(double pct) {
    if (pct < 100.0) return FG_LRED;
    if (pct < 120.0) return FG_YELLOW;
    if (pct < 150.0) return FG_LGREEN;
    if (pct < 200.0) return FG_LCYAN;
    return FG_LMAGENTA;
}

void animatePercent(double pct, int color) {
    int steps = 20;
    for (int i = 0; i <= steps; i++) {
        double v = pct * i / steps;
        ostringstream oss;
        oss << "\r      " << col(FG_LWHITE) << "Completion: "
            << col(color) << fixed << setprecision(1) << setw(6) << v
            << "%" << CLR_LINE;
        cout << oss.str() << flush;
        sleepMs(30);
    }
    ostringstream oss;
    oss << "\r      " << col(FG_LWHITE) << "Completion: "
        << col(color) << fixed << setprecision(1) << setw(6) << pct
        << "%" << CLR_LINE;
    cout << oss.str() << flush;
}

// ================== 三选一 BUFF ==================
BuffId showBuffChoice(GameState& g) {
    (void)g;
    vector<BuffId> pool;
    for (int i = 0; i < NUM_BUFFS; i++) pool.push_back(BUFFS[i].id);

    // ... 加权抽样（省略，和之前一样）...

//    int sel = 0;
    const int BOX_INNER = 41;

    // 边框
    string border;
    {
        ostringstream oss;
        oss << "      +";
        for (int i = 0; i < BOX_INNER; i++) oss << "=";
        oss << "+\n";
        border = oss.str();
    }

    // ? 改成 lambda（C++11 支持）
    auto centerIn = [](const string& txt, int width) -> string {
        int w = displayWidth(txt);
        if (w >= width) return truncByWidth(txt, width);
        int left  = (width - w) / 2;
        int right = width - w - left;
        string s;
        s.append(left, ' ');
        s += txt;
        s.append(right, ' ');
        return s;
    };

    while (true) {
        ostringstream o;
        o << CLEAR;
        o << col(FG_LMAGENTA);
        o << "\n\n";
        o << border;
        {
            string title = "C H O O S E   A   B U F F";
            // ? 调用 lambda
            string inner = centerIn(title, BOX_INNER - 2);
            o << "      | " << inner << " |\n";
        }
        o << border;
        o << "\n";
        // ... 剩下的和之前一样
    }
}

// ================== 玩一关 ==================
int playLevel(GameState& g) {
    if (g.endless) initEndlessLevel(g, g.endlessWave);
    else           initLevel(g, g.save.level);
    g.achToasts.clear();

    chrono::steady_clock::time_point levelStart = chrono::steady_clock::now();
    chrono::steady_clock::time_point lastTime   = levelStart;

    while (true) {
        chrono::steady_clock::time_point now = chrono::steady_clock::now();
        double dt = chrono::duration_cast<chrono::microseconds>(now - lastTime)
                        .count() / 1e6;
        if (dt > DT_MAX) dt = DT_MAX;
        if (dt < 0.0)    dt = 0.0;
        lastTime = now;

        while (plat::kbhit()) {
            KeyEvent ev = readKey();
            if (ev.key == KEY_SPACE) {
                if (!g.paused && g.state == 0) g.state = 1;
            } else if (ev.key == KEY_CHAR && ev.ch == 'p') {
                g.paused = !g.paused;
            } else if ((ev.key == KEY_CHAR && ev.ch == 'q') || ev.key == KEY_ESC) {
                long long ms = chrono::duration_cast<chrono::milliseconds>(
                    chrono::steady_clock::now() - levelStart).count();
                g.save.stats.totalPlayMs += ms;
                return -1;
            }
        }

        if (!g.paused) update(g, dt);
        draw(g);

        bool timeout = (g.timeLeft <= 0);
        bool empty   = isLevelCleared(g);

        if (timeout || empty) {
            long long ms = chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - levelStart).count();
            g.save.stats.totalPlayMs += ms;
            double elapsedSec = ms / 1000.0;

            int lv = g.endless ? g.endlessWave : g.level;
            if (!g.endless && lv >= 1 && lv <= MAX_LEVEL &&
                g.score > g.save.stats.bestScore[lv])
                g.save.stats.bestScore[lv] = g.score;
            if (g.save.gold > g.save.stats.maxGold)
                g.save.stats.maxGold = g.save.gold;

            if (lv >= 5)  unlockAch(g, A_DEEP_DIVER);
            if (lv >= 10) unlockAch(g, A_LEVEL_10);
            if (lv >= 20) unlockAch(g, A_LEVEL_20);
            if (lv >= 30) unlockAch(g, A_LEVEL_30);

            if (g.endless) {
                if (lv >= 5)  unlockAch(g, A_ENDLESS_5);
                if (lv >= 10) unlockAch(g, A_ENDLESS_10);
                if (lv >= 20) unlockAch(g, A_ENDLESS_20);
                if (lv > g.save.stats.endlessBest)
                    g.save.stats.endlessBest = lv;
            }

            bool win = (g.score >= g.target);

            if (win) {
                if (g.score >= (int)(g.target * 1.5)) unlockAch(g, A_PERFECTIONIST);
                if (elapsedSec <= 30.0)               unlockAch(g, A_SPEEDRUNNER);
                if (g.levelBombCatch == 0)            unlockAch(g, A_UNTOUCHABLE);
                if (g.levelRockCatch == 0)            unlockAch(g, A_NO_ROCK_CLEAR);
                if (g.timeLeft < 3.0)                 unlockAch(g, A_TIME_LOW_WIN);
                if (g.levelBombCatch >= 3)            unlockAch(g, A_BOMB_LOVER);
                if (g.score >= 10000)                 unlockAch(g, A_10000_ONE_LEVEL);

                bool allMax = true;
                for (int i = 0; i < NUM_UPGRADES; i++)
                    if (g.save.upLv[i] < UPG[i].maxLv) { allMax = false; break; }
                if (allMax) unlockAch(g, A_ALL_UPGRADES);
            }

            if (g.bestCombo >= 10) unlockAch(g, A_COMBO_10);
            if (g.bestCombo >= 20) unlockAch(g, A_COMBO_20);

            return win ? 1 : 0;
        }
        sleepMs(FRAME_MS);
    }
}

// ================== 子界面：Upgrades ==================
int screenUpgrades(GameState& g) {
    int sel = 0;
    while (true) {
        clampUpgrades(g.save);

        ostringstream o;
        o << CLEAR;
        o << col(FG_LCYAN) << "\n  =========== UPGRADES ===========\n\n";
        o << col(FG_LYELLOW) << "  Gold: " << g.save.gold
          << col(FG_LWHITE)  << "      Next Level: " << g.save.level
          << "\n\n";

        for (int i = 0; i < NUM_UPGRADES; i += 2) {
            for (int k = 0; k < 2 && i + k < NUM_UPGRADES; k++) {
                int idx = i + k;
                int lv = clampVal(g.save.upLv[idx], 0, UPG[idx].maxLv);

                if (idx == sel) o << col(FG_LYELLOW) << "  >> ";
                else            o << "     ";

                o << col(idx == sel ? FG_LWHITE : FG_WHITE)
                  << UPG[idx].name << " " << lv << "/" << UPG[idx].maxLv;

                if (lv >= UPG[idx].maxLv) {
                    o << col(FG_LMAGENTA) << " MAX    ";
                } else {
                    int cost = UPG[idx].basePrice + lv * UPG[idx].priceStep;
                    bool canBuy = g.save.gold >= cost;
                    o << col(canBuy ? FG_LGREEN : FG_GRAY)
                      << (canBuy ? " " : "!")
                      << setw(5) << cost << "  ";
                }
            }
            o << CLR_LINE << "\n";
        }

        o << "\n" << col(FG_LMAGENTA) << "  --- Current Stats ---\n";
        o << col(FG_GRAY)
          << "  Speed x" << fixed << setprecision(2) << g.eff.speedMul
          << " | Wt -" << (int)(g.eff.powerBonus * 100) << "%"
          << " | +" << (int)g.eff.timeBonus << "s"
          << " | Bomb -" << (int)(g.eff.bombReduce * 100) << "%"
          << " | Mag +" << setprecision(1) << g.eff.magnet << "\n";
        o << col(FG_GRAY)
          << "  Luck x" << setprecision(2) << g.eff.luckMul
          << " | Crit " << (int)(g.eff.critChance * 100) << "%"
          << " | Gold x" << g.eff.goldMul
          << " | Bomb+" << g.eff.bombBonus
          << " | Tide " << (int)g.eff.tideInterval << "s"
          << " | Rich +" << g.eff.richBonus
          << " | Safety -" << g.eff.safetyBonus << "\n";

        o << "\n" << col(FG_LWHITE) << "  Selected: "
          << col(FG_LYELLOW) << UPG[sel].desc << "\n";
        o << "\n" << col(FG_GRAY)
          << "  [Up/Down] Select  [Enter] Buy  [Esc] Back\n";
        o << RESET;
        cout << o.str() << flush;

        KeyEvent ev;
        while (true) {
            if (plat::kbhit()) { ev = readKey(); break; }
            sleepMs(10);
        }

        if (ev.key == KEY_ESC || (ev.key == KEY_CHAR && ev.ch == 'q')) {
            writeSave(g.save);
            return 0;
        }
        if (ev.key == KEY_UP)
            sel = (sel + NUM_UPGRADES - 1) % NUM_UPGRADES;
        else if (ev.key == KEY_DOWN)
            sel = (sel + 1) % NUM_UPGRADES;
        else if (ev.key == KEY_ENTER) {
            int lv = g.save.upLv[sel];
            if (lv >= UPG[sel].maxLv) continue;
            int cost = UPG[sel].basePrice + lv * UPG[sel].priceStep;
            if (g.save.gold >= cost) {
                g.save.gold -= cost;
                g.save.upLv[sel]++;
                if (g.save.upLv[sel] > UPG[sel].maxLv)
                    g.save.upLv[sel] = UPG[sel].maxLv;
                g.applyEffects();
                writeSave(g.save);
                sndBuy();
                for (int i = 0; i < NUM_UPGRADES; i++)
                    if (g.save.upLv[i] >= UPG[i].maxLv)
                        unlockAch(g, A_MAXED_OUT);
                bool allMax = true;
                for (int i = 0; i < NUM_UPGRADES; i++)
                    if (g.save.upLv[i] < UPG[i].maxLv) { allMax = false; break; }
                if (allMax) unlockAch(g, A_ALL_UPGRADES);
            }
        }
    }
}

// ================== 子界面：Achievements ==================
int screenAchievements(GameState& g) {
    int unlocked = 0;
    for (int i = 0; i < A_NUM_ACH; i++) if (g.save.unlocked[i]) unlocked++;

    const int PAGE = 15;
    int page = 0;
    int totalPages = (A_NUM_ACH + PAGE - 1) / PAGE;

    while (true) {
        ostringstream o;
        o << CLEAR;
        o << col(FG_LCYAN) << "\n  ========= ACHIEVEMENTS =========\n\n";
        o << col(FG_LWHITE) << "  Unlocked: "
          << col(FG_LGREEN) << unlocked << "/" << A_NUM_ACH
          << col(FG_GRAY) << "      Page " << (page + 1)
          << "/" << totalPages << "\n\n";

        int start = page * PAGE;
        int end   = min(start + PAGE, (int)A_NUM_ACH);
        for (int i = start; i < end; i++) {
            bool un = g.save.unlocked[i];
            bool hid = ACHS[i].hidden && !un;
            if (un) {
                o << col(FG_LGREEN) << "  [V] " << ACHS[i].name
                  << col(FG_GRAY)   << "  - " << ACHS[i].desc << "\n";
            } else if (hid) {
                o << col(FG_GRAY) << "  [ ] ??? (hidden)\n";
            } else {
                o << col(FG_GRAY) << "  [ ] " << ACHS[i].name
                  << "  - " << ACHS[i].desc << "\n";
            }
        }

        o << "\n" << col(FG_GRAY)
          << "  [Left/Right] Page  [Esc] Back\n" << RESET;
        cout << o.str() << flush;

        KeyEvent ev;
        while (true) {
            if (plat::kbhit()) { ev = readKey(); break; }
            sleepMs(10);
        }
        if (ev.key == KEY_ESC || (ev.key == KEY_CHAR && ev.ch == 'q')) return 0;
        if (ev.key == KEY_LEFT || (ev.key == KEY_CHAR && ev.ch == 'a'))
            page = (page + totalPages - 1) % totalPages;
        if (ev.key == KEY_RIGHT || (ev.key == KEY_CHAR && ev.ch == 'd'))
            page = (page + 1) % totalPages;
    }
}

// ================== 子界面：Stats ==================
int screenStats(GameState& g) {
    const SaveData& s = g.save;

    int unlockedCnt = 0;
    for (int i = 0; i < A_NUM_ACH; i++) if (s.unlocked[i]) unlockedCnt++;
    int totalUpg = 0, maxUpg = 0;
    for (int i = 0; i < NUM_UPGRADES; i++) {
        totalUpg += s.upLv[i];
        maxUpg   += UPG[i].maxLv;
    }

    ostringstream o;
    o << CLEAR;
    o << col(FG_LCYAN) << "\n  ============ STATS ============\n\n";

    o << col(FG_LWHITE)
      << "  Total Gold Earned : " << col(FG_LYELLOW) << s.stats.maxGold << "\n"
      << col(FG_LWHITE)
      << "  Current Gold      : " << col(FG_LYELLOW) << s.gold << "\n"
      << col(FG_LWHITE)
      << "  Items Grabbed     : " << s.stats.totalGrabbed << "\n"
      << col(FG_LWHITE)
      << "  Diamonds          : " << col(FG_LCYAN) << s.stats.diamondCount << "\n"
      << col(FG_LWHITE)
      << "  Crabs             : " << col(FG_LRED) << s.stats.crabCount << "\n"
      << col(FG_LWHITE)
      << "  Mice              : " << col(FG_GRAY) << s.stats.mouseCount << "\n"
      << col(FG_LWHITE)
      << "  Play Time         : " << col(FG_LGREEN)
      << (s.stats.totalPlayMs / 1000) << "s\n"
      << col(FG_LWHITE)
      << "  Endless Best      : " << col(FG_LMAGENTA) << "W"
      << s.stats.endlessBest;
    if (s.stats.endlessCurrent > 0)
        o << col(FG_LYELLOW) << " (in progress W" << s.stats.endlessCurrent << ")";
    o << "\n"
      << col(FG_LWHITE)
      << "  Achievements      : " << col(FG_LGREEN)
      << unlockedCnt << "/" << A_NUM_ACH << "\n"
      << col(FG_LWHITE)
      << "  Upgrades          : " << col(FG_LGREEN)
      << totalUpg << "/" << maxUpg << "\n\n";

    o << col(FG_LMAGENTA) << "  --- Best Score per Level ---\n";
    bool any = false;
    for (int i = 1; i <= MAX_LEVEL; i++) {
        if (s.stats.bestScore[i] == 0) continue;
        any = true;
        o << col(FG_GRAY) << "  Lv." << setw(2) << i
          << " : " << col(FG_LYELLOW) << setw(6) << s.stats.bestScore[i] << "\n";
    }
    if (!any) o << col(FG_GRAY) << "  (no records yet)\n";

    o << "\n" << col(FG_LMAGENTA) << "  --- Top " << TOP_SCORES << " ---\n";
    bool anyTop = false;
    for (int i = 0; i < TOP_SCORES; i++) {
        const ScoreEntry& e = s.topScores[i];
        if (e.score == 0) continue;
        anyTop = true;
        o << col(FG_GRAY) << "  " << (i + 1) << ". "
          << col(FG_LYELLOW) << setw(6) << e.score
          << col(FG_GRAY) << "  Lv." << setw(2) << e.level
          << "  " << formatTimestamp(e.when) << "\n";
    }
    if (!anyTop) o << col(FG_GRAY) << "  (no scores yet)\n";

    o << "\n" << col(FG_GRAY) << "  [Esc] Back\n" << RESET;
    cout << o.str() << flush;

    KeyEvent ev;
    while (true) {
        if (plat::kbhit()) { ev = readKey(); break; }
        sleepMs(10);
    }
    return 0;
}

// ================== 结算界面 ==================
void showGameOver(const SaveData& s, const vector<string>& toasts,
                  bool endless, int wave, int score, int target)
{
    cout << CLEAR;
    cout << col(FG_LRED)
         << "\n\n      ===== GAME OVER =====\n\n";

    if (endless) {
        cout << col(FG_LMAGENTA) << "      Endless Wave: " << wave << "\n";
        cout << col(FG_LYELLOW)  << "      Best Wave   : "
             << s.stats.endlessBest << "\n\n";
    } else {
        cout << col(FG_LYELLOW) << "      Reached Level: " << s.level << "\n\n";
    }

    double pct = (target > 0) ? (double)score / target * 100.0 : 0.0;
    int pc = pctColor(pct);
    Rating r = getRating(pct);

    animateNumber(score,  "Score",  FG_LYELLOW, 8);
    cout << "\n";
    animateNumber(target, "Target", FG_GRAY, 8);
    cout << "\n\n";
    animateBar(score, target, 30);
    cout << "\n";

    animatePercent(pct, pc);
    cout << "\n";

    {
        ostringstream oss;
        oss << "      " << col(FG_LWHITE) << "Rating: "
            << col(r.color) << r.stars
            << col(FG_GRAY) << "  " << r.title;
        cout << oss.str() << CLR_LINE << "\n";
        cout << "      " << col(FG_GRAY) << r.cn << CLR_LINE << "\n\n";
    }

    cout << col(FG_LWHITE)
         << "      Total Gold   : " << col(FG_LYELLOW) << s.gold << "\n"
         << col(FG_LWHITE)
         << "      Max Gold     : " << col(FG_LYELLOW) << s.stats.maxGold << "\n"
         << col(FG_LWHITE)
         << "      Items Grabbed: " << s.stats.totalGrabbed << "\n"
         << col(FG_LWHITE)
         << "      Diamonds     : " << col(FG_LCYAN) << s.stats.diamondCount << "\n"
         << col(FG_LWHITE)
         << "      Crabs        : " << col(FG_LRED) << s.stats.crabCount << "\n"
         << col(FG_LWHITE)
         << "      Mice         : " << col(FG_GRAY) << s.stats.mouseCount << "\n"
         << col(FG_LWHITE)
         << "      Play Time    : " << col(FG_LGREEN)
         << (s.stats.totalPlayMs / 1000) << "s\n\n";

    animateToasts(toasts);

    cout << col(FG_WHITE)
         << "      Press any key to return to menu...\n"
         << RESET << flush;
    sndLose();
    waitAnyKey();
}

void showWin(const SaveData& s, int lv, int score, int target,
             const vector<string>& toasts, bool endless,
             const GameState& g)
{
    cout << CLEAR;
    if (endless) {
        cout << col(FG_LMAGENTA)
             << "\n\n      ===== ENDLESS Wave " << lv << " Cleared! =====\n\n";
    } else {
        cout << col(FG_LGREEN)
             << "\n\n      ===== Level " << lv << " Cleared! =====\n\n";
    }

    animateNumber(score,  "Score",  FG_LYELLOW, 8);
    cout << "\n";
    animateNumber(target, "Target", FG_GRAY, 8);
    cout << "\n\n";

    animateBar(score, target, 30);
    cout << "\n";

    double pct = (target > 0)
        ? (double)score / target * 100.0
        : 0.0;
    int pc = pctColor(pct);
    Rating r = getRating(pct);

    animatePercent(pct, pc);
    cout << "\n";

    {
        ostringstream oss;
        oss << "      " << col(FG_LWHITE) << "Rating: "
            << col(r.color) << r.stars
            << col(FG_GRAY) << "  " << r.title;
        cout << oss.str() << CLR_LINE << "\n";
        cout << "      " << col(FG_GRAY) << r.cn << CLR_LINE << "\n\n";
    }

    if (pct >= 200.0) {
        flashBanner("      *** OVERKILL! ***", FG_LMAGENTA, 3);
    } else if (pct >= 150.0) {
        flashBanner("      *** PERFECT! ***", FG_LMAGENTA, 2);
    } else if (pct >= 100.0) {
        flashBanner("      *** SUCCESS ***", FG_LGREEN, 2);
    }

    {
        int dur = (int)(nowSeconds() - g.levelStartTime);
        if (dur < 0) dur = 0;
        ostringstream oss;
        oss << "      " << col(FG_LMAGENTA) << "--- Level Stats ---\n"
            << col(FG_GRAY)
            << "      Gold caught   : " << col(FG_LYELLOW) << g.levelGoldCatch << "\n"
            << col(FG_GRAY)
            << "      Diamonds      : " << col(FG_LCYAN) << g.levelDiamondCatch << "\n"
            << col(FG_GRAY)
            << "      Crabs         : " << col(FG_LRED) << g.levelCrabCatch << "\n"
            << col(FG_GRAY)
            << "      Mice          : " << col(FG_GRAY) << g.levelMouseCatch << "\n"
            << col(FG_GRAY)
            << "      Rocks         : " << col(FG_GRAY) << g.levelRockCatch << "\n"
            << col(FG_GRAY)
            << "      Bombs         : " << col(FG_LRED) << g.levelBombCatch << "\n"
            << col(FG_GRAY)
            << "      Crits         : " << col(FG_LMAGENTA) << g.levelCritCount << "\n"
            << col(FG_GRAY)
            << "      Misses        : " << col(FG_GRAY) << g.levelMissCount << "\n"
            << col(FG_GRAY)
            << "      Best Combo    : " << col(FG_LMAGENTA) << g.bestCombo << "\n"
            << col(FG_GRAY)
            << "      Tides         : " << col(FG_LBLUE) << g.levelTideCount << "\n"
            << col(FG_GRAY)
            << "      Time Used     : " << col(FG_LGREEN) << dur << "s\n"
            << col(FG_GRAY)
            << "      Time Left     : " << col(FG_LGREEN)
            << (int)(g.timeLeft > 0 ? g.timeLeft : 0) << "s\n\n";
        cout << oss.str();
    }

    if (!endless && lv >= 1 && lv <= MAX_LEVEL) {
        cout << "      " << col(FG_LWHITE) << "Best Score (Lv." << lv << "): "
             << col(FG_LYELLOW) << s.stats.bestScore[lv]
             << col(FG_WHITE) << "\n";
    }
    cout << "      " << col(FG_LWHITE) << "Total Gold: "
         << col(FG_LYELLOW) << s.gold
         << col(FG_WHITE) << "\n\n";

    animateToasts(toasts);

    cout << col(FG_WHITE)
         << "      Press any key to return to menu...\n"
         << RESET << flush;
    sndWin();
    waitAnyKey();
}

// ================== 主菜单 ==================
enum MenuAction {
    MA_START = 0, MA_ENDLESS, MA_UPGRADES, MA_ACHIEVEMENTS, MA_STATS,
    MA_TOGGLE_SOUND, MA_RESET_SAVE, MA_QUIT, MA_NONE
};

MenuAction showMainMenu(GameState& g) {
    int sel = 0;
    const int N = 8;
    const char* labels[N] = {
        "Start Game", "Endless Mode", "Upgrades", "Achievements",
        "Stats", "Toggle Sound", "Reset Save", "Quit",
    };

    while (true) {
        ostringstream o;
        o << CLEAR;
        o << col(FG_LCYAN)
          << "\n\n      +=================================+\n"
          << "      |        G O L D  M I N E R       |\n"
          << "      +=================================+\n\n";

        o << col(FG_LYELLOW) << "      Gold: " << g.save.gold
          << col(FG_LWHITE)  << "      Level: " << g.save.level
          << col(FG_LMAGENTA) << "      Endless: W"
          << g.save.stats.endlessBest;
        if (g.save.stats.endlessCurrent > 0)
            o << col(FG_LYELLOW) << " (in progress W"
              << g.save.stats.endlessCurrent << ")";
        o << "\n\n";

        for (int i = 0; i < N; i++) {
            bool active = (i == sel);
            o << (active ? col(FG_LYELLOW) : col(FG_GRAY))
              << "        " << (active ? "> " : "  ")
              << labels[i]
              << (active ? " <" : "  ");
            if (i == 5)
                o << "   [Now: " << (g.save.soundOn ? "ON" : "OFF") << "]";
            o << "\n";
        }

        o << "\n" << col(FG_GRAY)
          << "      [Up/Down] Select   [Enter] Confirm   [Q] Quit\n";
        o << RESET;
        cout << o.str() << flush;

        KeyEvent ev;
        while (true) {
            if (plat::kbhit()) { ev = readKey(); break; }
            sleepMs(10);
        }

        if (ev.key == KEY_UP)        { sel = (sel + N - 1) % N; sndMenu(); }
        else if (ev.key == KEY_DOWN) { sel = (sel + 1) % N;     sndMenu(); }
        else if (ev.key == KEY_ESC || (ev.key == KEY_CHAR && ev.ch == 'q'))
            return MA_QUIT;
        else if (ev.key == KEY_ENTER) {
            switch (sel) {
                case 0: return MA_START;
                case 1: return MA_ENDLESS;
                case 2: return MA_UPGRADES;
                case 3: return MA_ACHIEVEMENTS;
                case 4: return MA_STATS;
                case 5: return MA_TOGGLE_SOUND;
                case 6: return MA_RESET_SAVE;
                case 7: return MA_QUIT;
            }
        }
        else if (ev.key == KEY_CHAR) {
            switch (ev.ch) {
                case 's': return MA_START;
                case 'e': return MA_ENDLESS;
                case 'u': return MA_UPGRADES;
                case 'a': return MA_ACHIEVEMENTS;
                case 'v': return MA_STATS;
                case 't': return MA_TOGGLE_SOUND;
                case 'r': return MA_RESET_SAVE;
            }
        }
    }
}

// ================== Reset Save 确认 ==================
bool confirmResetSave() {
    cout << CLEAR;
    cout << col(FG_LRED)
         << "\n\n      ===== WARNING =====\n\n"
         << col(FG_LYELLOW)
         << "      This will backup and delete your save file:\n"
         << "      " << SAVE_FILE << "\n\n"
         << col(FG_WHITE)
         << "      [Y] Yes, reset    [N] Cancel\n\n"
         << RESET << flush;

    KeyEvent ev;
    while (true) {
        if (plat::kbhit()) { ev = readKey(); break; }
        sleepMs(10);
    }
    return (ev.key == KEY_CHAR && ev.ch == 'y');
}

// ================== 主函数 ==================
int main(int argc, char** argv) {
    uint32_t seed = (uint32_t)chrono::steady_clock::now()
                                 .time_since_epoch().count();
    bool resetSave = false;

    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--no-color") g_noColor = true;
        else if (a == "--no-sound") g_soundOn = false;
        else if (a == "--replay" && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        }
        else if (a == "--reset-save") resetSave = true;
        else if (a == "--help" || a == "-h") {
            cout << "Usage: " << argv[0]
                 << " [--no-color] [--no-sound] [--replay SEED] [--reset-save]\n";
            return 0;
        }
    }
    seedRng(seed);

    plat::termInit();
    cout << HIDE_CUR << CLEAR << flush;

    if (resetSave) {
        ifstream test(SAVE_FILE.c_str());
        if (test) {
            string backup = SAVE_FILE + ".bak";
            if (backupFile(SAVE_FILE, backup))
                cout << col(FG_LYELLOW)
                     << "\n  Save backed up to: " << backup << "\n";
            remove(SAVE_FILE.c_str());
        }
        cout << col(FG_LGREEN) << "  Starting a fresh game.\n" << RESET;
        sleepMs(800);
    }

    GameState g;
    SaveResult sr = loadSaveEx();

    if (sr.status == SAVE_OK) {
        g.save = sr.data;
        if (sr.loadedVersion > 0 && sr.loadedVersion < SAVE_VERSION) {
            string backup = SAVE_FILE + ".v"
                          + to_string(sr.loadedVersion) + ".bak";
            if (backupFile(SAVE_FILE, backup))
                cout << col(FG_YELLOW)
                     << "  Migrating save v" << sr.loadedVersion
                     << " -> v" << SAVE_VERSION
                     << "  (backup: " << backup << ")\n";
            migrateSave(g.save, sr.loadedVersion);
            writeSave(g.save);
        }
    } else if (sr.status == SAVE_PARSE_ERROR) {
        string backup = (sr.loadedVersion > 0)
            ? SAVE_FILE + ".v" + to_string(sr.loadedVersion) + ".bak"
            : SAVE_FILE + ".broken.bak";
        if (backupFile(SAVE_FILE, backup))
            cout << col(FG_YELLOW)
                 << "\n  Save unreadable. Backup: " << backup
                 << "\n  Starting a new game.\n";
        g.save = newSave();
    } else {
        g.save = newSave();
    }

    clampUpgrades(g.save);
    if (g_soundOn) g_soundOn = g.save.soundOn;
    g.applyEffects();

    if (!g_noColor) {
        cout << col(FG_GRAY) << "  Seed: " << seed
             << "  (share with --replay " << seed << ")\n" << RESET;
    }

    while (true) {
        MenuAction act = showMainMenu(g);
        if (act == MA_QUIT) {
            writeSave(g.save);
            break;
        }
        switch (act) {
            case MA_TOGGLE_SOUND:
                g.save.soundOn = !g.save.soundOn;
                g_soundOn = g.save.soundOn;
                writeSave(g.save);
                break;

            case MA_UPGRADES:
                screenUpgrades(g);
                break;

            case MA_ACHIEVEMENTS:
                screenAchievements(g);
                break;

            case MA_STATS:
                screenStats(g);
                break;

            case MA_RESET_SAVE:
                if (confirmResetSave()) {
                    string backup = SAVE_FILE + ".bak";
                    backupFile(SAVE_FILE, backup);
                    g.save = newSave();
                    g.applyEffects();
                    writeSave(g.save);
                    cout << col(FG_LGREEN)
                         << "\n  Save reset. Backup: " << backup << "\n"
                         << RESET;
                    sleepMs(900);
                }
                break;

            case MA_START: {
                g.endless = false;
                g.endlessWave = 0;
                g.activeBuffs.clear();
                int res = playLevel(g);
                if (res == -1) { writeSave(g.save); break; }
                if (res == 1) {
                    int lv = g.save.level, sc = g.score, tg = g.target;
                    recordScore(g.save, sc, lv);
                    showWin(g.save, lv, sc, tg, g.achToasts, false, g);
                    g.save.level = (lv >= MAX_LEVEL) ? 1 : lv + 1;
                    writeSave(g.save);
                } else {
                    recordScore(g.save, g.score, g.save.level);
                    showGameOver(g.save, g.achToasts, false, 0,
                                 g.score, g.target);
                    g.save.level = 1;
                    writeSave(g.save);
                }
                break;
            }

            case MA_ENDLESS: {
                g.endless = true;
                g.endlessWave = max(1, g.save.stats.endlessCurrent);
                g.loadBuffsFromSave();
                g.applyEffects();

                if (g.save.stats.endlessCurrent > 0) {
                    cout << CLEAR;
                    cout << col(FG_LMAGENTA)
                         << "\n\n      Resuming Endless at Wave "
                         << g.endlessWave << "\n"
                         << col(FG_WHITE)
                         << "      Press any key to start...\n" << RESET << flush;
                    waitAnyKey();
                }

                while (true) {
                    int res = playLevel(g);
                    if (res == -1) {
                        g.save.stats.endlessCurrent = g.endlessWave;
                        g.syncBuffsToSave();
                        writeSave(g.save);
                        break;
                    }
                    if (res == 1) {
                        int lv = g.endlessWave, sc = g.score, tg = g.target;
                        showWin(g.save, lv, sc, tg, g.achToasts, true, g);

                        if (g.endlessWave % 5 == 0) {
                            cout << CLEAR;
                            cout << col(FG_LMAGENTA)
                                 << "\n\n      Wave " << g.endlessWave
                                 << " cleared! Choose a buff:\n";
                            sleepMs(900);

                            BuffId picked = showBuffChoice(g);
                            g.activeBuffs.push_back(picked);

                            cout << CLEAR;
                            for (int j = 0; j < NUM_BUFFS; j++) {
                                if (BUFFS[j].id == picked) {
                                    cout << col(FG_LGREEN)
                                         << "\n\n      Acquired: " << BUFFS[j].name
                                         << "\n      " << BUFFS[j].desc << "\n\n"
                                         << RESET;
                                    break;
                                }
                            }
                            sleepMs(1300);
                            g.applyEffects();
                        }

                        g.endlessWave++;
                        g.save.stats.endlessCurrent = g.endlessWave;
                        g.syncBuffsToSave();
                        writeSave(g.save);
                    } else {
                        showGameOver(g.save, g.achToasts, true,
                                     g.endlessWave, g.score, g.target);
                        g.save.stats.endlessCurrent = 0;
                        g.save.endlessBuffCount = 0;
                        g.syncBuffsToSave();
                        writeSave(g.save);
                        break;
                    }
                }
                g.endless = false;
                g.endlessWave = 0;
                g.activeBuffs.clear();
                break;
            }

            case MA_NONE:
            default:
                break;
        }
    }

    cout << SHOW_CUR << RESET << CLEAR;
    plat::termRestore();
    return 0;
}
