// Offline host test for the native front end (src/XeniaPlugin.cpp) with the real engine and your ROM, under ASan/UBSan (tests/run.sh).
// A fake VST2 host drives it in real time (the emulator thread fills its ring at its own pace): identity and frozen order, sound, value /
// option / PLAY-target / momentary / step / tile params, the parameter-0 nudge, RANDOM, favourites, a state round trip into a second
// instance, a 0.1.2 project chunk (tests/data/legacy-0.1.2.chunk, made by the released build itself) handed over before the boot, bad
// chunks, and a 48 kHz host. Prints OK at the end, or the failed checks and exits 1.
#include "mpc_vst2_abi.h"
#include "params_gen.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" AEffect* VSTPluginMain(audioMasterCallback);
extern "C" void xenia_dbg_tick(AEffect*);
extern "C" int xenia_dbg_ready(AEffect*);
extern "C" int xenia_dbg_engine(AEffect*, const char*, char*, int);

static std::atomic<int> g_automate[kNumParams];
static std::atomic<int> g_updateDisplay{0};
static int g_fail = 0;

static VstIntPtr host(AEffect*, VstInt32 op, VstInt32 idx, VstIntPtr, void*, float)
{
    if (op == audioMasterAutomate && idx >= 0 && idx < kNumParams) g_automate[idx]++;
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterUpdateDisplay) g_updateDisplay++;
    return 0;
}

#define CHECK(c, ...) do { if (c) { std::printf("  ok   "); } else { std::printf("  FAIL "); g_fail++; } std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

static int idx(const char* key)
{
    for (int i = 0; i < kNumParams; i++) if (!std::strcmp(g_pspecs[i].key, key)) return i;
    std::printf("no param %s\n", key); std::exit(2);
}
static float norm(int i, float plain) { const PSpec& s = g_pspecs[i]; return s.max > s.min ? (plain - s.min) / (s.max - s.min) : 0.f; }
static float plainOf(AEffect* e, int i) { const PSpec& s = g_pspecs[i]; return s.min + e->getParameter(e, i) * (s.max - s.min); }
static std::string eng(AEffect* e, const char* k) { char b[4096]; xenia_dbg_engine(e, k, b, sizeof b); return b; }
static int engi(AEffect* e, const char* k) { return std::atoi(eng(e, k).c_str()); }
static std::string disp(AEffect* e, int i) { char b[128] = {0}; e->dispatcher(e, effGetParamDisplay, i, 0, b, 0); return b; }

static double g_rms;
static void run(AEffect* e, int blocks, const std::vector<std::vector<uint8_t>>& midi = {}, int frames = 128)
{
    std::vector<float> L((size_t)frames), R((size_t)frames);
    float* out[2] = {L.data(), R.data()};
    double acc = 0; long n = 0;
    for (int b = 0; b < blocks; b++)
    {
        if (b == 0 && !midi.empty())
        {
            std::vector<VstMidiEvent> ev(midi.size());
            std::vector<char> buf(sizeof(VstEvents) + sizeof(VstEvent*) * midi.size());
            VstEvents* evs = reinterpret_cast<VstEvents*>(buf.data());
            evs->numEvents = (VstInt32)midi.size();
            for (size_t k = 0; k < midi.size(); k++)
            {
                std::memset(&ev[k], 0, sizeof ev[k]);
                ev[k].type = kVstMidiType; ev[k].byteSize = sizeof(VstMidiEvent);
                for (size_t j = 0; j < 3 && j < midi[k].size(); j++) ev[k].midiData[j] = (char)midi[k][j];
                evs->events[k] = reinterpret_cast<VstEvent*>(&ev[k]);
            }
            e->dispatcher(e, effProcessEvents, 0, 0, evs, 0);
        }
        e->processReplacing(e, nullptr, out, frames);
        for (int i = 0; i < frames; i++) { acc += (double)L[i] * L[i] + (double)R[i] * R[i]; n += 2; }
        std::this_thread::sleep_for(std::chrono::microseconds(2900 * frames / 128));
    }
    g_rms = n ? std::sqrt(acc / n) : 0;
}
static void settle(AEffect* e, int blocks = 40) { run(e, blocks); xenia_dbg_tick(e); }

static AEffect* open(const char* what, float rate = 44100.f)
{
    AEffect* e = VSTPluginMain(host);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, rate);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);
    std::printf("%s: opened\n", what);
    return e;
}
static bool waitReady(AEffect* e)
{
    for (int t = 0; t < 300 && !xenia_dbg_ready(e); t++) run(e, 34);   // up to ~30 s
    run(e, 300);                                                       // the ROM's sound list and the first sound arrive
    xenia_dbg_tick(e);
    return xenia_dbg_ready(e);
}
static std::vector<uint8_t> chunk(AEffect* e)
{
    void* p = nullptr;
    const VstIntPtr n = e->dispatcher(e, effGetChunk, 0, 0, &p, 0);
    return n > 0 && p ? std::vector<uint8_t>((uint8_t*)p, (uint8_t*)p + n) : std::vector<uint8_t>();
}

int main(int argc, char** argv)
{
    const char* legacyPath = argc > 1 ? argv[1] : "tests/data/legacy-0.1.2.chunk";
    const int BANK = idx("bank_index"), PRESET = idx("preset"), NEXT = idx("preset_next"), PNAME = idx("preset_name"), CUT = idx("F1Cutoff"),
              FX = idx("EffectType"), PLAY1 = idx("play1"), PLAYV1 = idx("play_v1"), SLOT5 = idx("patch_slot_5"), GAIN = idx("gain");

    std::printf("== ROM sound names (scanned once per ROM, then cached)\n");
    {
        const char* pf = std::getenv("MPC_PLUGIN_FOLDER");
        const std::string cache = std::string(pf ? pf : ".") + "/xenia/rom-names.cache";
        std::remove(cache.c_str());
        AEffect* w = open("W");
        bool done = waitReady(w);
        for (int t = 0; t < 400 && done && engi(w, "names_done") == 0; t++) run(w, 34);
        FILE* f = std::fopen(cache.c_str(), "r");
        int lines = 0;
        for (int ch; f && (ch = std::fgetc(f)) != EOF;) lines += ch == '\n';
        if (f) std::fclose(f);
        CHECK(done && engi(w, "names_done") == 1 && lines == 201, "the first boot scans the 200 names and caches them (%d lines)", lines);
        w->dispatcher(w, effMainsChanged, 0, 0, nullptr, 0);
        w->dispatcher(w, effClose, 0, 0, nullptr, 0);
    }

    std::printf("== identity\n");
    AEffect* a = open("A");
    char name[64] = {0};
    a->dispatcher(a, effGetEffectName, 0, 0, name, 0);
    CHECK(a->uniqueID == (VstInt32)0x58656e41, "uid XenA (%08x)", (unsigned)a->uniqueID);
    CHECK(!std::strcmp(name, XENIA_EXPECT_NAME), "name \"%s\"", name);
    CHECK(a->numParams == 346, "346 params: 332 frozen + 14 appended (%d)", a->numParams);
    CHECK(a->dispatcher(a, effGetVendorVersion, 0, 0, nullptr, 0) == 2000, "version 2000");
    {
        int bad = 0, i = 0;
        FILE* f = std::fopen("params.lock", "r");
        char line[128];
        while (f && std::fgets(line, sizeof line, f))   // 253..331: the 1.x popup flags' slots (ui_open + reserved), named "<key>__open" in the lock
        {
            line[std::strcspn(line, "\r\n")] = 0;
            if (!line[0]) continue;
            const bool slot = i >= kNumEngineParams && i < kNumEngineParams + kNumPopups;
            if (slot ? !std::strstr(line, "__open") : std::strcmp(line, g_pspecs[i].key) != 0) bad++;
            i++;
        }
        if (f) std::fclose(f);
        CHECK(f && bad == 0 && i == 346, "order = params.lock (%d keys, %d differ)", i, bad);
        CHECK(!std::strcmp(g_pspecs[0].key, "bank_index") && !std::strcmp(g_pspecs[253].key, "ui_open"), "0 = bank_index, 253 = ui_open");
    }
    CHECK(waitReady(a), "booted (%s)", eng(a, "loading_status").c_str());
    CHECK(engi(a, "names_done") == 1 && eng(a, "patch_slot_30").size() > 4, "the names come from the cache at once (tile 30 \"%s\")", eng(a, "patch_slot_30").c_str());

    std::printf("== sound\n");
    run(a, 200, {{0x90, 60, 100}});
    const double on = g_rms;
    run(a, 600, {{0x80, 60, 0}});   // the release (a pad's release can be louder than its slow attack window)
    run(a, 200);
    const double off = g_rms;          // ~2 s after the note off
    if (on <= 1e-3)   // diagnose: the engine's view, then a second note a second later
    {
        std::printf("    (silent: %s; %s; %s; ready %d)\n", eng(a, "loading_status").c_str(), eng(a, "engine_stats").c_str(), eng(a, "preset_name").c_str(), xenia_dbg_ready(a));
        run(a, 340, {{0x80, 60, 0}});
        run(a, 200, {{0x90, 62, 100}});
        std::printf("    (second note: rms %.4f; %s)\n", g_rms, eng(a, "engine_stats").c_str());
        run(a, 100, {{0x80, 62, 0}});
    }
    CHECK(on > 1e-3, "note on is audible (rms %.4f)", on);
    CHECK(off < on, "note off decays (rms %.4f)", off);

    std::printf("== value params\n");
    a->setParameter(a, CUT, norm(CUT, 64));
    settle(a);
    CHECK(engi(a, "F1Cutoff") == 64, "F1 CUTOFF 64 -> engine %d", engi(a, "F1Cutoff"));
    a->setParameter(a, FX, 1.0f);
    settle(a);
    CHECK(engi(a, "EffectType") == 10 && disp(a, FX) == "Mod Delay", "EFFECT last option -> engine %d, \"%s\"", engi(a, "EffectType"), disp(a, FX).c_str());
    {   // a Q-Link nudge smaller than one step still moves one step (settle)
        const float cur = plainOf(a, CUT);
        a->setParameter(a, CUT, norm(CUT, cur) + 0.3f / 127.f);
        settle(a);
        CHECK(engi(a, "F1Cutoff") == (int)cur + 1 && std::lround(plainOf(a, CUT)) == (int)cur + 1,
              "a sub-step nudge up moves one step (%d -> engine %d, host %.0f)", (int)cur, engi(a, "F1Cutoff"), plainOf(a, CUT));
    }
    std::printf("== PLAY knob target\n");
    a->setParameter(a, PLAY1, norm(PLAY1, 7));   // Filter 2 Cutoff
    settle(a, 60);
    xenia_dbg_tick(a);
    CHECK(eng(a, "play1") == g_pspecs[PLAY1].choices[7], "PLAY 1 target -> engine \"%s\"", eng(a, "play1").c_str());
    CHECK(std::lround(plainOf(a, PLAY1)) == 7, "host PLAY 1 target stays %.0f", plainOf(a, PLAY1));
    a->setParameter(a, PLAYV1, norm(PLAYV1, 33));
    settle(a);
    CHECK(engi(a, "F2Cutoff") == 33, "PLAY 1 knob 33 -> Filter 2 Cutoff %d", engi(a, "F2Cutoff"));

    std::printf("== preset step, readback, text\n");
    const int p0 = engi(a, "preset");
    const std::string n0 = disp(a, PNAME);
    const float c0 = a->getParameter(a, PNAME);
    for (int i = 0; i < kNumParams; i++) g_automate[i] = 0;
    const int ud0 = g_updateDisplay;
    a->setParameter(a, NEXT, 1.0f);
    settle(a, 300);
    xenia_dbg_tick(a);
    CHECK(engi(a, "preset") == p0 + 1, "PRESET + : engine preset %d -> %d", p0, engi(a, "preset"));
    CHECK(std::lround(plainOf(a, PRESET)) == p0 + 1, "host PRESET follows (%.0f)", plainOf(a, PRESET));
    CHECK(a->getParameter(a, NEXT) == 0.f, "PRESET + dropped back to 0");
    CHECK(disp(a, PNAME) != n0 && a->getParameter(a, PNAME) != c0, "preset name \"%s\" -> \"%s\", its counter moved", n0.c_str(), disp(a, PNAME).c_str());
    int told = 0;
    for (int i = 0; i < kNumParams; i++) told += g_automate[i] > 0;
    CHECK(told > 20 && g_updateDisplay > ud0, "the host heard about the new sound (%d params, %d display updates)", told, g_updateDisplay - ud0);
    {
        int bad = 0;
        for (int i = 1; i < kNumEngineParams; i++)
        {
            const PSpec& s = g_pspecs[i];
            if (!(s.flags & kPolled) || !std::strncmp(s.key, "play", 4)) continue;
            const int ev = std::min(std::max(engi(a, s.key), (int)s.min), (int)s.max);
            if (std::lround(plainOf(a, i)) != ev) { if (bad < 5) std::printf("    %s host %.0f engine %d\n", s.key, plainOf(a, i), engi(a, s.key)); bad++; }
        }
        CHECK(bad == 0, "every value param matches the engine after the load (%d differ)", bad);
    }

    std::printf("== parameter 0 (BANK)\n");
    const int b0 = engi(a, "bank_index");
    a->setParameter(a, 0, 1.0f); a->setParameter(a, 0, norm(0, (float)b0));   // the host's prepare nudge
    settle(a);
    CHECK(engi(a, "bank_index") == b0, "a nudge to the end and back changes nothing (%d)", engi(a, "bank_index"));
    a->setParameter(a, BANK, norm(BANK, 1));
    run(a, 30);
    xenia_dbg_tick(a);
    settle(a, 300);
    CHECK(engi(a, "bank_index") == 1, "a held change applies (bank %d)", engi(a, "bank_index"));

    std::printf("== tiles\n");
    a->setParameter(a, SLOT5, 1.0f);
    settle(a, 300);
    CHECK(engi(a, "preset") == 4 && a->getParameter(a, SLOT5) == 1.f && std::lround(plainOf(a, PRESET)) == 4, "tile 5 picks preset 5 and is lit (%d)", engi(a, "preset"));
    a->setParameter(a, SLOT5, 0.0f);   // the host toggles a lit tile off: it stays lit
    settle(a);
    CHECK(a->getParameter(a, SLOT5) == 1.f && !disp(a, SLOT5).empty(), "a lit tile stays lit, shows \"%s\"", disp(a, SLOT5).c_str());

    std::printf("== random sound, favourites\n");
    {
        const int RND = idx("random_patch"), ADD = idx("fav_add"), DEL = idx("fav_del");
        int changes = 0, bankB = 0;
        for (int k = 0; k < 6; k++)
        {
            const int s0 = engi(a, "bank_index") * 1000 + engi(a, "preset");
            a->setParameter(a, RND, 1.0f);
            settle(a, 300);
            xenia_dbg_tick(a);
            const int s1 = engi(a, "bank_index") * 1000 + engi(a, "preset");
            changes += s1 != s0;
            bankB += s1 >= 1000;
            if (k == 0)
                CHECK(a->getParameter(a, RND) == 0.f && std::lround(plainOf(a, PRESET)) == s1 % 1000 && std::lround(plainOf(a, BANK)) == s1 / 1000,
                      "RANDOM: %d -> %d, host BANK/PRESET follow, button back to 0", s0, s1);
            a->setParameter(a, RND, 0.0f);
        }
        CHECK(changes == 6, "six RANDOM presses, six new sounds (%d)", changes);
        std::printf("    (%d of 6 landed outside bank A)\n", bankB);
        CHECK(a->numPrograms == 65, "65 programs (current + 64 favourites): %d", a->numPrograms);
        const int fb = engi(a, "bank_index"), fp = engi(a, "preset");
        a->setParameter(a, CUT, norm(CUT, 21));
        settle(a);
        const std::string nm = eng(a, "preset_name");
        a->setParameter(a, ADD, 1.0f);
        settle(a);
        char pn[64] = {};
        a->dispatcher(a, effGetProgramNameIndexed, 1, 0, pn, 0);
        std::string want = nm.size() > 4 ? nm.substr(4) : nm;
        while (!want.empty() && want.back() == ' ') want.pop_back();
        CHECK(std::string(pn) == "1 " + want && a->getParameter(a, ADD) == 0.f, "+FAV: program 1 = \"%s\", button back to 0", pn);
        a->setParameter(a, ADD, 0.0f);
        a->setParameter(a, NEXT, 1.0f);
        settle(a, 300);
        a->setParameter(a, NEXT, 0.0f);
        settle(a);
        a->dispatcher(a, effSetProgram, 0, 1, nullptr, 0);
        settle(a, 400);
        xenia_dbg_tick(a);
        CHECK(engi(a, "bank_index") == fb && engi(a, "preset") == fp && engi(a, "F1Cutoff") == 21, "PRESET 1 recalls %d/%d with F1 CUTOFF %d",
              engi(a, "bank_index"), engi(a, "preset"), engi(a, "F1Cutoff"));
        a->setParameter(a, DEL, 1.0f);
        settle(a);
        a->dispatcher(a, effGetProgramNameIndexed, 1, 0, pn, 0);
        CHECK(std::string(pn) == "1 --", "-FAV: program 1 = \"%s\"", pn);
        a->setParameter(a, DEL, 0.0f);
        a->dispatcher(a, effSetProgram, 0, 0, nullptr, 0);
        settle(a);
    }

    std::printf("== user banks\n");
    {
        a->setParameter(a, BANK, norm(BANK, 0));   // start on bank A: the tile page follows the current bank (RANDOM moved it)
        run(a, 30); xenia_dbg_tick(a);
        settle(a, 100);
        const int nb = engi(a, "bank_count");
        CHECK(nb > 2, "user banks found: %d banks (ROM A/B + %d)", nb, nb - 2);
        const int BP = idx("bank_page_next"), BT1 = idx("bank_slot_1"), BT3 = idx("bank_slot_3");
        CHECK(disp(a, BT1) == "BANK A" && disp(a, BT3).size() > 0, "bank tiles: \"%s\", tile 3 \"%s\"", disp(a, BT1).c_str(), disp(a, BT3).c_str());
        a->setParameter(a, BT3, 1.0f);
        settle(a, 200);
        xenia_dbg_tick(a);
        const std::string un = eng(a, "preset_name");
        CHECK(engi(a, "bank_index") == 2 && engi(a, "preset") == 0 && std::lround(plainOf(a, BANK)) == 2 && un.size() > 4,
              "tile 3 loads user bank \"%s\" sound 1: \"%s\" (host BANK %.0f)", eng(a, "bank_name").c_str(), un.c_str(), plainOf(a, BANK));
        CHECK(a->getParameter(a, BT3) == 1.f, "the user bank's tile is lit");
        run(a, 200, {{0x90, 60, 100}});
        CHECK(g_rms > 1e-3, "a user sound plays (rms %.4f)", g_rms);
        run(a, 300, {{0x80, 60, 0}});
        a->setParameter(a, NEXT, 1.0f);
        settle(a, 100);
        a->setParameter(a, NEXT, 0.0f);
        settle(a);
        CHECK(engi(a, "preset") == 1 && eng(a, "preset_name") != un, "PRESET + in a user bank: \"%s\"", eng(a, "preset_name").c_str());
        a->setParameter(a, idx("preset_prev"), 1.0f); settle(a, 60); a->setParameter(a, idx("preset_prev"), 0.0f); settle(a);
        a->setParameter(a, idx("preset_prev"), 1.0f); settle(a, 60); a->setParameter(a, idx("preset_prev"), 0.0f); settle(a);
        const int cnt = engi(a, "preset") + 1;
        CHECK(cnt >= 1 && eng(a, "patch_page_text").find("/") != std::string::npos, "PRESET - wraps to the bank's last sound (%d), %s", cnt, eng(a, "patch_page_text").c_str());
        if (nb > 10)
        {
            a->setParameter(a, BP, 1.0f); settle(a); a->setParameter(a, BP, 0.0f); settle(a);
            CHECK(eng(a, "bank_page_text").rfind("BANKS 2", 0) == 0 && disp(a, BT1) != "BANK A", "bank page 2: %s, tile 1 \"%s\"", eng(a, "bank_page_text").c_str(), disp(a, BT1).c_str());
            a->setParameter(a, idx("bank_page_prev"), 1.0f); settle(a); a->setParameter(a, idx("bank_page_prev"), 0.0f); settle(a);
        }
        int user = 0;
        for (int k = 0; k < 8; k++)
        {
            a->setParameter(a, idx("random_patch"), 1.0f);
            settle(a, 60);
            a->setParameter(a, idx("random_patch"), 0.0f);
            settle(a, 10);
            user += engi(a, "bank_index") >= 2;
        }
        CHECK(user > 0, "RANDOM reaches the user banks (%d of 8)", user);
        // a user sound survives a save and reload (BANK 2 = the first user bank; the tile page follows RANDOM's bank)
        a->setParameter(a, BANK, norm(BANK, 2));
        run(a, 30); xenia_dbg_tick(a);
        settle(a, 100);
        a->setParameter(a, PRESET, norm(PRESET, 0));
        settle(a, 100);
        a->setParameter(a, NEXT, 1.0f); settle(a, 100); a->setParameter(a, NEXT, 0.0f); settle(a);
        a->setParameter(a, CUT, norm(CUT, 33));
        settle(a, 60);
        const std::string uname = eng(a, "preset_name");
        const std::vector<uint8_t> cu = chunk(a);
        AEffect* u = open("U");
        CHECK(u->dispatcher(u, effSetChunk, 0, (VstIntPtr)cu.size(), (void*)cu.data(), 0) == 1, "U takes a chunk saved on a user sound (before its boot)");
        CHECK(waitReady(u), "U ready");
        settle(u, 300);
        xenia_dbg_tick(u);
        CHECK(engi(u, "bank_index") == 2 && engi(u, "preset") == 1 && engi(u, "F1Cutoff") == 33 && eng(u, "preset_name") == uname,
              "U: user bank %d sound %d \"%s\", F1 CUTOFF %d", engi(u, "bank_index"), engi(u, "preset") + 1, eng(u, "preset_name").c_str(), engi(u, "F1Cutoff"));
        run(u, 200, {{0x90, 60, 100}});
        CHECK(g_rms > 1e-3, "U plays the user sound (rms %.4f)", g_rms);
        run(u, 50, {{0x80, 60, 0}});
        u->dispatcher(u, effMainsChanged, 0, 0, nullptr, 0);
        u->dispatcher(u, effClose, 0, 0, nullptr, 0);
    }

    std::printf("== state round trip\n");
    a->setParameter(a, BANK, norm(BANK, 0));
    run(a, 30); xenia_dbg_tick(a);
    a->setParameter(a, PRESET, norm(PRESET, 12));
    settle(a, 400);
    a->setParameter(a, CUT, norm(CUT, 77));
    a->setParameter(a, GAIN, norm(GAIN, 300));
    settle(a, 60);
    const std::string nameA = eng(a, "preset_name");
    const std::vector<uint8_t> ca = chunk(a);
    CHECK(ca.size() > 600 && !std::memcmp(ca.data(), "MPCP", 4), "A saves an SDK chunk (%zu bytes)", ca.size());
    AEffect* b = open("B");
    CHECK(waitReady(b), "B ready");
    CHECK(b->dispatcher(b, effSetChunk, 0, (VstIntPtr)ca.size(), (void*)ca.data(), 0) == 1, "B loads A's chunk");
    settle(b, 400);
    xenia_dbg_tick(b);
    CHECK(engi(b, "bank_index") == 0 && engi(b, "preset") == 12 && engi(b, "F1Cutoff") == 77 && engi(b, "gain") == 300,
          "B: A/%d, F1 CUTOFF %d (the edit survives), GAIN %d", engi(b, "preset"), engi(b, "F1Cutoff"), engi(b, "gain"));
    CHECK(eng(b, "preset_name") == nameA && std::lround(plainOf(b, CUT)) == 77, "B: \"%s\", host F1 CUTOFF %.0f", eng(b, "preset_name").c_str(), plainOf(b, CUT));

    std::printf("== a 0.1.2 project\n");
    std::vector<uint8_t> legacy;
    if (FILE* f = std::fopen(legacyPath, "rb")) { int c; while ((c = std::fgetc(f)) != EOF) legacy.push_back((uint8_t)c); std::fclose(f); }
    CHECK(legacy.size() > 600 && legacy[0] == '{', "read %s (%zu bytes)", legacyPath, legacy.size());
    AEffect* c = open("C");   // as the MPC does: the project's chunk arrives straight after the plugin opens, before the boot
    CHECK(c->dispatcher(c, effSetChunk, 0, (VstIntPtr)legacy.size(), legacy.data(), 0) == 1, "C takes the 0.1.2 chunk at once");
    {
        const std::vector<uint8_t> early = chunk(c);
        const std::string s(early.begin(), early.end());
        CHECK(s.find("\"preset\":37") != std::string::npos && s.find("\"single\":\"") != std::string::npos,
              "a save before the boot keeps the project's state with its sound");
    }
    CHECK(waitReady(c), "C ready");
    settle(c, 400);
    xenia_dbg_tick(c);
    CHECK(engi(c, "bank_index") == 1 && engi(c, "preset") == 37, "C: bank %d preset %d (B 038)", engi(c, "bank_index"), engi(c, "preset"));
    CHECK(engi(c, "F1Cutoff") == 40 && std::lround(plainOf(c, CUT)) == 40, "C: F1 CUTOFF %d, host %.0f (the 0.1.2 edit)", engi(c, "F1Cutoff"), plainOf(c, CUT));
    CHECK(engi(c, "gain") == 180 && std::lround(plainOf(c, GAIN)) == 180, "C: GAIN %d", engi(c, "gain"));
    run(c, 200, {{0x90, 64, 100}});
    const double dark = g_rms;
    run(c, 100, {{0x80, 64, 0}});
    c->setParameter(c, CUT, norm(CUT, 127));   // the project's sound has F1 CUTOFF 40 (quiet but not silent): open it to hear it
    settle(c);
    run(c, 200, {{0x90, 64, 100}});
    CHECK(dark > 1e-4 && g_rms > 1e-3, "C plays (rms %.4f at cutoff 40, %.4f open)", dark, g_rms);
    run(c, 50, {{0x80, 64, 0}});

    std::printf("== bad chunks\n");
    const char junk[] = "not a chunk";
    CHECK(c->dispatcher(c, effSetChunk, 0, sizeof junk, (void*)junk, 0) == 0, "junk is refused");
    const uint8_t cut[] = {'M', 'P', 'C', 'P', 2, 0, 0, 0, 200, 0, 0, 0, 5};
    CHECK(c->dispatcher(c, effSetChunk, 0, sizeof cut, (void*)cut, 0) == 0, "a truncated SDK chunk is refused");
    settle(c);
    CHECK(engi(c, "preset") == 37, "C still on preset 38");

    std::printf("== 48 kHz host\n");
    for (AEffect* e : {a, b, c}) { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0); e->dispatcher(e, effClose, 0, 0, nullptr, 0); }
    AEffect* d = open("D", 48000.f);
    CHECK(waitReady(d), "D ready");
    run(d, 200, {{0x90, 60, 100}});
    CHECK(g_rms > 1e-3, "D plays at 48 kHz (rms %.4f; %s; %s; %s)", g_rms, eng(d, "loading_status").c_str(), eng(d, "engine_stats").c_str(), eng(d, "preset_name").c_str());
    run(d, 50, {{0x80, 60, 0}});
    d->dispatcher(d, effMainsChanged, 0, 0, nullptr, 0);
    d->dispatcher(d, effClose, 0, 0, nullptr, 0);

    if (g_fail) { std::printf("FAILED: %d checks\n", g_fail); return 1; }
    std::printf("OK\n");
    return 0;
}
