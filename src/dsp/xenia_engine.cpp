/* Xenia for MPC: gearmulator's Waldorf Microwave XT emulation (xtLib: the XT's 68k firmware plus its DSP56303 voice DSP) as an
 * mpc-vst-plugins engine (wrapper/engine.h). GPL-3.0.
 *
 * One emulator thread per instance runs the device through gearmulator's synthLib::Plugin (which resamples the XT's 40 kHz to
 * the host's 44.1 kHz), fills a ring of int16 stereo, and render() only drains it. Everything the host does reaches the device
 * as MIDI or sysex queued from the host's thread: notes and controllers as they are, parameters as XT single parameter change
 * sysex, sounds as a ROM single copied into the edit buffer. The sound parameter table is generated (tools/gen_params.py ->
 * xenia_params.h). Single mode, one part: the XT's Multi mode, Wave editor and Global settings are not exposed.
 *
 * ROM: <data_dir>/roms/ (scanned by xt::RomLoader: a 256 kB `.bin` full ROM, two 128 kB halves, or an OS update `.mid`).
 * None ships with the plugin. */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <string>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

extern "C" {
#include "engine.h"
}

#include "synthLib/deviceTypes.h"
#include "synthLib/midiTypes.h"
#include "synthLib/plugin.h"
#include "synthLib/romLoader.h"
#include "xtLib/xtDevice.h"
#include "xtLib/xtMidiTypes.h"
#include "xtLib/xtRomLoader.h"

#include "xenia_params.h"

namespace {

constexpr int kRate = 44100, kBlock = 128;
constexpr int kRingFrames = 16384;               // power of two
constexpr int kTargetFill = 1024;                // frames the emulator thread keeps queued (~23 ms)
constexpr int kBanks = 2, kPerBank = 100, kPatchSlots = 30, kDumpLen = 265;
constexpr int kNameOff = 247, kNameLen = 16;     // in a single dump including its sysex header
constexpr int kParamOff = 7;                     // first parameter byte of a single dump
constexpr int kEditBuffer = 0x20;                // LocationH::SingleEditBufferSingleMode
constexpr int kDeviceId = 0x7f;                  // omni

using Dump = std::vector<uint8_t>;

FILE *g_log = nullptr;
void xlog(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* CPU ticks used so far by every thread of this process named "DSP" (gearmulator's emulator threads); polled every ~2 s */
long dsp_thread_ticks() {
    long total = 0;
    DIR *d = opendir("/proc/self/task");
    if (!d) return 0;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[64], line[512];
        snprintf(path, sizeof path, "/proc/self/task/%s/comm", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        const bool dsp = fgets(line, sizeof line, f) && strncmp(line, "DSP", 3) == 0;
        fclose(f);
        if (!dsp) continue;
        snprintf(path, sizeof path, "/proc/self/task/%s/stat", e->d_name);
        f = fopen(path, "r");
        if (!f) continue;
        if (fgets(line, sizeof line, f)) {
            const char *q = strrchr(line, ')');
            unsigned long ut = 0, st = 0;
            if (q && sscanf(q + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &st) == 2) total += (long)(ut + st);
        }
        fclose(f);
    }
    closedir(d);
    return total;
}

enum CmdType : uint8_t { C_MIDI, C_PARAM, C_PRESET, C_PANIC, C_LOADDUMP, C_INIT };
struct Cmd {
    CmdType type = C_MIDI;
    uint8_t m[3] = {0, 0, 0};
    int a = 0, b = 0;
    Dump dump;
};

uint8_t checksum(const Dump &d) {     // over the parameter bytes, stored before the closing F7
    uint8_t c = 0;
    for (size_t i = kParamOff; i + 2 < d.size(); i++) c += d[i];
    return c & 0x7f;
}

struct Inst {
    std::string dir;
    std::thread th;
    std::atomic<bool> stop{false}, ready{false}, failed{false};

    /* audio ring: written by the emulator thread, drained by render() */
    std::vector<int16_t> ring = std::vector<int16_t>(kRingFrames * 2);
    std::atomic<uint32_t> wr{0}, rd{0};
    std::atomic<uint32_t> underruns{0}, blocks{0};
    std::atomic<int> dsp_cpu{0};

    /* host -> emulator */
    std::mutex qm;
    std::vector<Cmd> q;

    /* what the UI reads: the edit buffer's raw parameter bytes, the current sound */
    std::atomic<uint8_t> raw[256];
    std::atomic<int> bank{0}, preset{0}, page{0};
    std::atomic<int> clock{100}, gain{100};
    std::atomic<uint32_t> rev{1};
    std::mutex sm;                                            // names, status, the edit buffer dump
    char status[96] = "Starting...";
    char rom_name[96] = "";
    char names[kBanks][kPerBank][kNameLen + 1] = {};
    bool have[kBanks][kPerBank] = {};
    char cur_name[kNameLen + 1] = "";
    Dump cur;                                                 // the edit buffer as a dump (265 bytes) once a sound is loaded

    void bump() { rev.fetch_add(1, std::memory_order_relaxed); }
    void set_status(const char *s) {
        { std::lock_guard<std::mutex> l(sm); snprintf(status, sizeof status, "%s", s); }
        bump();
    }
    void push(Cmd &&c) { std::lock_guard<std::mutex> l(qm); if (q.size() < 4096) q.push_back(std::move(c)); }
};

std::unordered_map<std::string, int> &xt_index() {
    static std::unordered_map<std::string, int> m = [] {
        std::unordered_map<std::string, int> r;
        for (int i = 0; i < XT_NUM_PARAMS; i++) r[g_xt_params[i].key] = i;
        return r;
    }();
    return m;
}

int value_to_raw(const xt_param_t &p, int v) {
    int raw;
    switch (p.kind) {
    case 1: raw = p.lo + v; break;
    case 2: raw = 64 + 12 * v; break;
    case 3: raw = (p.map && v >= 0 && v < p.nmap) ? p.map[v] : p.lo; break;
    default: raw = v + p.off; break;
    }
    return std::min<int>(std::max<int>(raw, p.lo), p.hi);
}

int raw_to_value(const xt_param_t &p, int raw) {
    switch (p.kind) {
    case 1: return raw - p.lo;
    case 2: return (int)std::lround((raw - 64) / 12.0);
    case 3:
        for (int i = 0; i < p.nmap; i++) if (p.map[i] == raw) return i;
        return 0;
    default: return raw - p.off;
    }
}

synthLib::SMidiEvent sysex_event(std::initializer_list<uint8_t> bytes) {
    synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
    e.sysex.assign(bytes.begin(), bytes.end());
    return e;
}

synthLib::SMidiEvent dump_event(const Dump &d) {
    synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
    e.sysex.assign(d.begin(), d.end());
    return e;
}

/* A request for ROM single (bank, program) or for the edit buffer */
synthLib::SMidiEvent request_single(int buf, int loc) {
    return sysex_event({0xf0, 0x3e, 0x0e, kDeviceId, (uint8_t)xt::SysexCommand::SingleRequest, (uint8_t)buf, (uint8_t)loc, 0xf7});
}

synthLib::SMidiEvent param_change(int idx, int raw) {
    return sysex_event({0xf0, 0x3e, 0x0e, kDeviceId, (uint8_t)xt::SysexCommand::SingleParameterChange, 0x00,
                        (uint8_t)((idx >> 7) & 0x7f), (uint8_t)(idx & 0x7f), (uint8_t)(raw & 0x7f), 0xf7});
}

void tune_thread() {
    int nice_val = 5;                      // the emulator thread yields to MPC's own audio and UI threads
    if (const char *e = getenv("XENIA_NICE")) nice_val = atoi(e);
    if (nice_val) setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), nice_val);
}

/* The sound the host last asked for; the dump arrives from the device (or its cache) some blocks later */
struct Want { bool active = false; int bank = 0, prog = 0; int waited = 0; };

void apply_loaded(Inst *in, const Dump &d, int bank, int prog) {
    Dump e = d;
    e[5] = kEditBuffer;
    e[6] = 0;
    e[e.size() - 2] = checksum(e);
    {
        std::lock_guard<std::mutex> l(in->sm);
        in->cur = e;
        char nm[kNameLen + 1];
        for (int i = 0; i < kNameLen; i++) nm[i] = (char)(e[kNameOff + i] >= 32 && e[kNameOff + i] < 127 ? e[kNameOff + i] : ' ');
        nm[kNameLen] = 0;
        for (int i = kNameLen - 1; i >= 0 && nm[i] == ' '; i--) nm[i] = 0;
        snprintf(in->cur_name, sizeof in->cur_name, "%s", nm);
    }
    for (int i = 0; i < XT_NUM_PARAMS; i++) in->raw[g_xt_params[i].idx].store(e[kParamOff + g_xt_params[i].idx]);
    if (bank >= 0) { in->bank = bank; in->preset = prog; }
    in->bump();
}

void emu_main(Inst *in) {
    tune_thread();
    in->set_status("Looking for the XT ROM...");
    std::string roms = in->dir + "/roms";
    synthLib::RomLoader::addSearchPath(roms);
    xt::Rom rom = xt::RomLoader::findROM();
    if (!rom.isValid()) {
        in->set_status("No Microwave XT ROM in xenia/roms/");
        in->failed = true;
        in->bump();
        return;
    }
    { std::lock_guard<std::mutex> l(in->sm); snprintf(in->rom_name, sizeof in->rom_name, "%s", rom.getFilename().c_str()); }
    in->set_status("Booting the XT (this takes a while)...");
    xlog("[emu] ROM %s", rom.getFilename().c_str());

    std::unique_ptr<xt::Device> dev;
    std::unique_ptr<synthLib::Plugin> plugin;
    try {
        synthLib::DeviceCreateParams p;
        p.romData = rom.getData();
        p.romName = rom.getFilename();
        p.hostSamplerate = (float)kRate;
        dev.reset(new xt::Device(p));
        if (!dev->isValid()) throw std::runtime_error("the XT firmware did not start");
        dev->setDspClockPercent((uint32_t)in->clock.load());
        plugin.reset(new synthLib::Plugin(dev.get(), [](synthLib::Device *d) { return d; }));
        plugin->setMidiClockEnabled(false);
        plugin->setHostSamplerate((float)kRate, 0.0f);
        plugin->setBlockSize(kBlock);
    } catch (const std::exception &e) {
        char b[96];
        snprintf(b, sizeof b, "XT start failed: %.70s", e.what());
        in->set_status(b);
        in->failed = true;
        xlog("[emu] %s", b);
        return;
    }
    xlog("[emu] booted");

    float outl[kBlock], outr[kBlock];
    synthLib::TAudioInputs inputs = {};
    synthLib::TAudioOutputs outputs = {};
    outputs[0] = outl;
    outputs[1] = outr;
    std::vector<synthLib::SMidiEvent> midiOut;

    Want want;
    bool dirty[256] = {};
    uint8_t pending[256] = {};
    int next_req = 0, applied_clock = in->clock.load();
    const int first_bank = in->bank.load() % kBanks;
    bool harvest_done = false;
    long last_ticks = 0;
    auto last_t = std::chrono::steady_clock::now();
    uint32_t block_no = 0;

    auto ask_for_sound = [&](int bank, int prog) {
        want.active = true; want.bank = bank; want.prog = prog; want.waited = 0;
        xlog("[sound] request single %d/%d", bank, prog);
        plugin->addMidiEvent(request_single(bank, prog));
    };

    auto produce = [&](bool to_ring) {
        std::vector<Cmd> cmds;
        { std::lock_guard<std::mutex> l(in->qm); cmds.swap(in->q); }
        for (auto &c : cmds) {
            switch (c.type) {
            case C_MIDI: {
                synthLib::SMidiEvent e(synthLib::MidiEventSource::Host, c.m[0], c.m[1], c.m[2]);
                plugin->addMidiEvent(e);
                break;
            }
            case C_PARAM:
                pending[c.a & 255] = (uint8_t)c.b;
                dirty[c.a & 255] = true;
                break;
            case C_PRESET:
                memset(dirty, 0, sizeof dirty);
                ask_for_sound(c.a, c.b);
                break;
            case C_PANIC:
                for (uint8_t ch = 0; ch < 16; ch++) {
                    plugin->addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, (uint8_t)(0xb0 | ch), 123, 0));
                    plugin->addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, (uint8_t)(0xb0 | ch), 120, 0));
                }
                break;
            case C_LOADDUMP:
                memset(dirty, 0, sizeof dirty);
                want.active = false;
                plugin->addMidiEvent(dump_event(c.dump));
                apply_loaded(in, c.dump, -1, 0);
                break;
            case C_INIT:
                for (int i = 0; i < XT_NUM_PARAMS; i++) {
                    pending[g_xt_params[i].idx] = (uint8_t)g_xt_params[i].def;
                    dirty[g_xt_params[i].idx] = true;
                    in->raw[g_xt_params[i].idx].store((uint8_t)g_xt_params[i].def);     // the value cache the UI and the state read
                }
                in->bump();
                break;
            }
        }
        // parameter writes, coalesced: the XT's MIDI input is a 31250 baud UART, so a sweep must not queue hundreds of messages
        int sent = 0;
        for (int i = 0; i < 256 && sent < 6; i++) {
            if (!dirty[i]) continue;
            dirty[i] = false;
            plugin->addMidiEvent(param_change(i, pending[i]));
            sent++;
        }
        // names of the ROM sounds, a few requests per block until every one is known (current bank first)
        if (!harvest_done && !want.active && (block_no & 3) == 0) {
            while (next_req < kBanks * kPerBank) {
                const int bank = (first_bank + next_req / kPerBank) % kBanks, prog = next_req % kPerBank;
                next_req++;
                std::lock_guard<std::mutex> l(in->sm);
                if (in->have[bank][prog]) continue;
                plugin->addMidiEvent(request_single(bank, prog));
                break;
            }
            if (next_req >= kBanks * kPerBank) harvest_done = true;
        }
        if (want.active && ++want.waited % 200 == 0) { xlog("[sound] still waiting for %d/%d", want.bank, want.prog); plugin->addMidiEvent(request_single(want.bank, want.prog)); }

        plugin->process(inputs, outputs, kBlock, 120.0f, 0.0f, false, false);
        block_no++;

        midiOut.clear();
        plugin->getMidiOut(midiOut);
        for (auto &e : midiOut) {
            const auto &sx = e.sysex;
            if (sx.size() != (size_t)kDumpLen || sx[0] != 0xf0 || sx[1] != 0x3e || sx[2] != 0x0e || sx[4] != (uint8_t)xt::SysexCommand::SingleDump) continue;
            const int bank = sx[5], prog = sx[6];
            if (bank < kBanks && prog < kPerBank) {
                std::lock_guard<std::mutex> l(in->sm);
                for (int i = 0; i < kNameLen; i++) {
                    const uint8_t ch = sx[kNameOff + i];
                    in->names[bank][prog][i] = (char)(ch >= 32 && ch < 127 ? ch : ' ');
                }
                in->names[bank][prog][kNameLen] = 0;
                for (int i = kNameLen - 1; i >= 0 && in->names[bank][prog][i] == ' '; i--) in->names[bank][prog][i] = 0;
                in->have[bank][prog] = true;
                if ((block_no & 7) == 0 || want.active) in->bump();
            }
            if (want.active && bank == want.bank && prog == want.prog) {
                want.active = false;
                xlog("[sound] loaded %d/%d \"%s\"", bank, prog, in->names[bank][prog]);
                Dump d(sx.begin(), sx.end());
                d[5] = kEditBuffer;
                d[6] = 0;
                d[d.size() - 2] = checksum(d);
                plugin->addMidiEvent(dump_event(d));
                apply_loaded(in, d, bank, prog);
            }
        }

        if (!to_ring) return;
        const float g = in->gain.load() / 100.0f * 32767.0f;
        uint32_t w = in->wr.load(std::memory_order_relaxed);
        for (int i = 0; i < kBlock; i++) {
            const float l = std::max(-32768.0f, std::min(32767.0f, outl[i] * g));
            const float r = std::max(-32768.0f, std::min(32767.0f, outr[i] * g));
            in->ring[(w & (kRingFrames - 1)) * 2] = (int16_t)l;
            in->ring[(w & (kRingFrames - 1)) * 2 + 1] = (int16_t)r;
            w++;
        }
        in->wr.store(w, std::memory_order_release);
    };

    // first sound, and a short run so the ring starts full
    ask_for_sound(in->bank.load(), in->preset.load());
    in->set_status("Warming up...");
    // (the first sound's edit buffer must have arrived, or the first notes play the ROM's power-on state: silence)
    for (int i = 0; (i < kTargetFill / kBlock + 2 || (want.active && i < 4000)) && !in->stop; i++) produce(true);
    in->ready = true;
    in->set_status("Ready");
    xlog("[emu] ready");

    while (!in->stop) {
        const uint32_t fill = in->wr.load(std::memory_order_relaxed) - in->rd.load(std::memory_order_acquire);
        if (fill >= (uint32_t)kTargetFill || fill + kBlock > (uint32_t)kRingFrames) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }
        const int want_clock = in->clock.load();
        if (want_clock != applied_clock) {
            dev->setDspClockPercent((uint32_t)want_clock);
            applied_clock = want_clock;
        }
        produce(true);
        in->blocks.fetch_add(1, std::memory_order_relaxed);

        if ((block_no % 690) == 0) {         // ~2 s
            const auto now = std::chrono::steady_clock::now();
            const long tk = dsp_thread_ticks();
            const double dt = std::chrono::duration<double>(now - last_t).count();
            if (dt > 0.1) in->dsp_cpu = (int)((tk - last_ticks) / 100.0 / dt * 100.0);
            last_ticks = tk;
            last_t = now;
        }
    }
    xlog("[emu] stopping");
    plugin.reset();
    dev.reset();
}

/* ------------------------------------------------------------------ the engine interface */

void *eng_create(const char *dir) {
    if (!g_log) g_log = fopen(getenv("XENIA_LOG") ? getenv("XENIA_LOG") : "/tmp/xenia.log", "a");     // small: boot, sound changes, dumps
    Inst *in = new Inst();
    in->dir = dir ? dir : "";
    for (auto &r : in->raw) r.store(0);
    for (int i = 0; i < XT_NUM_PARAMS; i++) in->raw[g_xt_params[i].idx].store((uint8_t)g_xt_params[i].def);
    in->th = std::thread(emu_main, in);
    return in;
}

void eng_destroy(void *p) {
    Inst *in = (Inst *)p;
    if (!in) return;
    in->stop = true;
    if (in->th.joinable()) in->th.join();
    delete in;
}

void eng_midi(void *p, const uint8_t *msg, int len) {
    Inst *in = (Inst *)p;
    if (!in || len < 1 || len > 3 || msg[0] < 0x80 || msg[0] >= 0xf0) return;
    const uint8_t st = msg[0] & 0xf0;
    if (st == 0xc0 && len >= 2) {                         // program change: a sound of the current bank
        if (msg[1] < kPerBank) { Cmd c; c.type = C_PRESET; c.a = in->bank.load(); c.b = msg[1]; in->preset = msg[1]; in->push(std::move(c)); }
        return;
    }
    if (st == 0xb0 && len >= 3 && msg[1] == 0) {          // bank select MSB: bank A / B, takes effect with the next program change
        if (msg[2] < kBanks) in->bank = msg[2];
        return;
    }
    if (st == 0xb0 && len >= 3 && msg[1] == 32) return;
    Cmd c;
    c.type = C_MIDI;
    c.m[0] = msg[0];
    c.m[1] = len > 1 ? msg[1] : 0;
    c.m[2] = len > 2 ? msg[2] : 0;
    in->push(std::move(c));
}

int slot_key(const char *key, const char *prefix, int max, bool *on) {
    const size_t n = strlen(prefix);
    if (strncmp(key, prefix, n) != 0) return 0;
    char *end = nullptr;
    const long v = strtol(key + n, &end, 10);
    if (end == key + n || v < 1 || v > max) return 0;
    if (*end == 0) { *on = false; return (int)v; }
    if (strcmp(end, "_on") == 0) { *on = true; return (int)v; }
    return 0;
}

int pages(int count = kPerBank) { return (count + kPatchSlots - 1) / kPatchSlots; }

void select_sound(Inst *in, int bank, int prog) {
    bank = std::max(0, std::min(kBanks - 1, bank));
    prog = std::max(0, std::min(kPerBank - 1, prog));
    xlog("[sound] select bank %d preset %d", bank, prog);
    in->bank = bank;
    in->preset = prog;
    Cmd c;
    c.type = C_PRESET;
    c.a = bank;
    c.b = prog;
    in->push(std::move(c));
    in->bump();
}

int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

void restore_state(Inst *in, const char *s) {
    auto num = [&](const char *k, int def) {
        const char *q = strstr(s, k);
        return q ? atoi(q + strlen(k)) : def;
    };
    in->bank = std::max(0, std::min(kBanks - 1, num("\"bank\":", 0)));
    in->preset = std::max(0, std::min(kPerBank - 1, num("\"preset\":", 0)));
    in->gain = std::max(10, std::min(400, num("\"gain\":", 100)));
    in->clock = std::max(10, std::min(100, num("\"clock\":", 100)));
    const char *h = strstr(s, "\"single\":\"");
    if (h) {
        h += 10;
        Dump d;
        while (hexval(h[0]) >= 0 && hexval(h[1]) >= 0) { d.push_back((uint8_t)(hexval(h[0]) * 16 + hexval(h[1]))); h += 2; }
        if (d.size() == (size_t)kDumpLen && d.front() == 0xf0 && d.back() == 0xf7) {
            Cmd c;
            c.type = C_LOADDUMP;
            c.dump = std::move(d);
            in->push(std::move(c));
            return;
        }
    }
    select_sound(in, in->bank, in->preset);
}

void eng_set(void *p, const char *key, const char *val) {
    Inst *in = (Inst *)p;
    if (!in || !key || !val) return;
    bool on = false;
    int n;
    if ((n = slot_key(key, "bank_slot_", kBanks, &on)) && !on) { if (atoi(val)) select_sound(in, n - 1, in->preset); return; }
    if ((n = slot_key(key, "patch_slot_", kPatchSlots, &on)) && !on) { if (atoi(val) && in->page * kPatchSlots + n - 1 < kPerBank) select_sound(in, in->bank, in->page * kPatchSlots + n - 1); return; }
    if (!strcmp(key, "patch_page_prev")) { if (atoi(val) && in->page > 0) { in->page--; in->bump(); } return; }
    if (!strcmp(key, "patch_page_next")) { if (atoi(val) && in->page + 1 < pages()) { in->page++; in->bump(); } return; }
    if (!strcmp(key, "preset")) { select_sound(in, in->bank, atoi(val)); in->page = in->preset / kPatchSlots; return; }
    if (!strcmp(key, "bank_index")) { select_sound(in, atoi(val), in->preset); return; }
    // the stepper arrows send these verbs (momentary, 1 on press); step with wrap-around
    if (!strcmp(key, "preset_next") || !strcmp(key, "preset_prev")) {
        if (atoi(val)) {
            const int d = !strcmp(key, "preset_next") ? 1 : kPerBank - 1;
            select_sound(in, in->bank, (in->preset + d) % kPerBank);
            in->page = in->preset / kPatchSlots;
        }
        return;
    }
    if (!strcmp(key, "bank_next") || !strcmp(key, "bank_prev")) {
        if (atoi(val)) select_sound(in, (in->bank + (!strcmp(key, "bank_next") ? 1 : kBanks - 1)) % kBanks, in->preset);
        return;
    }
    if (!strcmp(key, "all_notes_off")) { if (atoi(val)) { Cmd c; c.type = C_PANIC; in->push(std::move(c)); } return; }
    if (!strcmp(key, "init_patch")) { if (atoi(val)) { Cmd c; c.type = C_INIT; in->push(std::move(c)); } return; }
    if (!strcmp(key, "dsp_clock")) { in->clock = std::max(10, std::min(100, atoi(val))); return; }
    if (!strcmp(key, "gain")) { in->gain = std::max(10, std::min(400, atoi(val))); return; }
    if (!strcmp(key, "state")) { restore_state(in, val); return; }
    auto it = xt_index().find(key);
    if (it == xt_index().end()) return;
    const xt_param_t &q = g_xt_params[it->second];
    const int raw = value_to_raw(q, atoi(val));
    in->raw[q.idx].store((uint8_t)raw);
    Cmd c;                                              // the emulator thread coalesces: a = index, b = raw
    c.type = C_PARAM;
    c.a = q.idx;
    c.b = raw;
    in->push(std::move(c));
}

int put(char *buf, int n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r < 0 ? 0 : std::min(r, n - 1);
}

int eng_get(void *p, const char *key, char *buf, int n) {
    Inst *in = (Inst *)p;
    if (!in || !key || n < 2) return -1;
    bool on = false;
    int k;
    if ((k = slot_key(key, "bank_slot_", kBanks, &on))) {
        if (on) return put(buf, n, "%d", k - 1 == in->bank ? 1 : 0);
        return put(buf, n, "BANK %c", 'A' + k - 1);
    }
    if ((k = slot_key(key, "patch_slot_", kPatchSlots, &on))) {
        const int idx = in->page * kPatchSlots + k - 1;
        if (on) return put(buf, n, "%d", idx == in->preset ? 1 : 0);
        if (idx >= kPerBank) return put(buf, n, "%s", "");
        std::lock_guard<std::mutex> l(in->sm);
        const int b = in->bank;
        return put(buf, n, "%03d %s", idx + 1, in->have[b][idx] ? in->names[b][idx] : "");
    }
    if (!strcmp(key, "patch_page_text")) return put(buf, n, "PAGE %d / %d", in->page + 1, pages());
    if (!strcmp(key, "preset")) return put(buf, n, "%d", in->preset.load());
    if (!strcmp(key, "bank_index")) return put(buf, n, "%d", in->bank.load());
    if (!strcmp(key, "bank_name")) return put(buf, n, "BANK %c", 'A' + in->bank.load());
    if (!strcmp(key, "preset_name")) {
        std::lock_guard<std::mutex> l(in->sm);
        const int b = in->bank, pr = in->preset;
        // the loaded sound's own name once it is in the edit buffer, the ROM table's before that
        const char *nm = in->cur_name[0] ? in->cur_name : (in->have[b][pr] ? in->names[b][pr] : "");
        return put(buf, n, "%03d %s", pr + 1, nm);
    }
    if (!strcmp(key, "rom_name")) { std::lock_guard<std::mutex> l(in->sm); return put(buf, n, "%s", in->rom_name[0] ? in->rom_name : "-"); }
    if (!strcmp(key, "loading_status")) { std::lock_guard<std::mutex> l(in->sm); return put(buf, n, "%s", in->status); }
    if (!strcmp(key, "engine_stats")) return put(buf, n, "DROPOUTS %u   DSP CPU %d%%", in->underruns.load(), in->dsp_cpu.load());
    if (!strcmp(key, "dsp_clock")) return put(buf, n, "%d", in->clock.load());
    if (!strcmp(key, "gain")) return put(buf, n, "%d", in->gain.load());
    if (!strcmp(key, "display_rev")) return put(buf, n, "%u", in->rev.load());
    if (!strcmp(key, "state")) {
        std::lock_guard<std::mutex> l(in->sm);
        int off = put(buf, n, "{\"state_version\":1,\"bank\":%d,\"preset\":%d,\"gain\":%d,\"clock\":%d", in->bank.load(), in->preset.load(),
                      in->gain.load(), in->clock.load());
        if (in->ready && in->cur.size() == (size_t)kDumpLen && off + 540 < n) {
            Dump d = in->cur;
            for (int i = 0; i < XT_NUM_PARAMS; i++) d[kParamOff + g_xt_params[i].idx] = in->raw[g_xt_params[i].idx].load();
            d[d.size() - 2] = checksum(d);
            off += put(buf + off, n - off, ",\"single\":\"");
            static const char hx[] = "0123456789abcdef";
            for (uint8_t b : d) { buf[off++] = hx[b >> 4]; buf[off++] = hx[b & 15]; }
            buf[off] = 0;
        }
        off += put(buf + off, n - off, "}");
        return off;
    }
    auto it = xt_index().find(key);
    if (it != xt_index().end()) {
        const xt_param_t &q = g_xt_params[it->second];
        return put(buf, n, "%d", raw_to_value(q, in->raw[q.idx].load()));
    }
    return -1;
}

void eng_render(void *p, int16_t *out, int frames) {
    Inst *in = (Inst *)p;
    if (!in || !in->ready.load(std::memory_order_acquire)) { memset(out, 0, (size_t)frames * 4); return; }
    uint32_t r = in->rd.load(std::memory_order_relaxed);
    const uint32_t avail = in->wr.load(std::memory_order_acquire) - r;
    const int got = (int)std::min<uint32_t>(avail, (uint32_t)frames);
    for (int i = 0; i < got; i++, r++) {
        out[i * 2] = in->ring[(r & (kRingFrames - 1)) * 2];
        out[i * 2 + 1] = in->ring[(r & (kRingFrames - 1)) * 2 + 1];
    }
    in->rd.store(r, std::memory_order_release);
    if (got < frames) {
        memset(out + got * 2, 0, (size_t)(frames - got) * 4);
        in->underruns.fetch_add(1, std::memory_order_relaxed);
    }
}

const mpc_engine_t engine = {eng_create, eng_destroy, eng_midi, eng_set, eng_get, eng_render, nullptr, nullptr};

}  // namespace

extern "C" const mpc_engine_t *mpc_engine(void) { return &engine; }
