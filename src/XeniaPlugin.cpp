// Xenia for MPC: the native front end (framework/mpc_plugin.h) around the unchanged Xenia engine (src/dsp/xenia_engine.cpp:
// gearmulator's Microwave XT emulation on its own emulator thread, rendering 44.1 kHz int16 stereo into a ring).
//
// Signal flow: host MIDI (sample accurate) -> engine midi -> engine render (drains the ring) -> float outputs, through a linear
// resampler when the host does not run at 44.1 kHz.
//
// Parameters: the released 0.1.2 wrapper build's 332 in the same order (params.lock -> tools/gen_sdk_params.py -> params_gen.h),
// then the appended front-end keys.
//   * value params (kPolled): a host change goes to the engine as its own key; options as their index, the PLAY knob targets
//     (play1..play4) as their label (the engine matches the label). Whole numbers and options settle toward the direction of a move,
//     so a small Q-Link turn moves one step (the 1.x rule). A sound / bank load, a state load or a PLAY target change bumps the engine's
//     "display_rev": the worker then reads every value back and tells the host.
//   * momentary verbs (kMomentary): 0 -> 1 fires once, then the param drops back to 0. BANK / PRESET < > (kStep) are the engine's own
//     verbs; list tiles (kTile, BANKS page) fire on a tap and are lit from the engine's "<key>_on".
//   * text readouts (kText): the value text is the engine's text; the value is a counter bumped when the text changes, because the
//     host only re-reads a param's text when its value moves.
//   * front-end keys (kLocal): RANDOM picks a random sound from every loaded bank (the engine's "random_sound"). Favourites are the
//     MPC's PRESET menu (VST programs): +FAV saves the current sound with its edits (the engine state) in the first free slot, -FAV
//     deletes the picked favourite. Program 0 is "Current sound". The slots live in <plugin folder>/xenia-favourites.dat.
//   * ui_open (kUi): which drawn pick-list the skin shows; the reserved slots (kReserved) hold the 1.x popup flags' indices.
// Parameter 0 is BANK. The host nudges parameter 0 on every prepare: a change applies only after it has held for ~40 ms.
// State: the engine's own "state" JSON is the truth; it travels as the MPC chunk's extra state, and nothing is written to the engine
// while the MPC chunk restores parameter values. A project saved by the 1.x wrapper build stored only that JSON as its chunk: it is
// still read (effSetChunk is wrapped below). The MPC hands the chunk over before the emulator has booted: the last state given is
// kept and returned from a save until the engine is ready.

#include "mpc_plugin.h"

extern "C" {
#include "engine.h"
}
#include "params_gen.h"

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef XENIA_NAME
#define XENIA_NAME "Xenia"
#endif
#ifndef XENIA_UID
#define XENIA_UID 'X', 'e', 'n', 'A'
#endif
#define XENIA_FOURCC(...) MPC_FOURCC(__VA_ARGS__)   // expands XENIA_UID before MPC_FOURCC counts its arguments
#ifndef XENIA_VERSION
#define XENIA_VERSION 2000
#endif

namespace {

const int kEngineRate = 44100;
const int kChunk = 128;
const int kStateMax = 4096;   // the engine's state is ~650 bytes (header + 265-byte sound as hex)

mpc::PluginInfo makeInfo()
{
    mpc::PluginInfo i;
    i.name = XENIA_NAME;
    i.vendor = "sd88me";
    i.uniqueId = XENIA_FOURCC(XENIA_UID);
    i.version = XENIA_VERSION;
    i.isInstrument = true;
    i.numInputs = 0;
    i.numOutputs = 2;
    return i;
}

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint32_t fnv(const char* s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

bool isState(const std::string& s) { return s.size() > 2 && s[0] == '{' && s.find("\"state_version\"") != std::string::npos; }
bool isPlayTarget(const char* k) { return !std::strncmp(k, "play", 4) && k[4] >= '1' && k[4] <= '4' && !k[5]; }

thread_local int t_pushing = -1;   // the parameter the worker is telling the host about right now (its echo is ignored)

} // namespace

class XeniaPlugin : public mpc::Plugin
{
public:
    XeniaPlugin() : mpc::Plugin(makeInfo())
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const PSpec& s = g_pspecs[i];
            mpc::Param p = (s.flags & kChoice)
                ? mpc::choiceParam(s.key, s.name, std::vector<std::string>(s.choices, s.choices + s.nchoices), (int)s.def)
                : mpc::intParam(s.key, s.name, (int)s.min, (int)s.max, (int)s.def);
            p.automatable = !(s.flags & kNoAuto);
            addParam(p);
        }
        const size_t n = (size_t)kNumParams;
        held_ = new std::atomic<bool>[n];
        release_ = new std::atomic<bool>[n];
        hostMoved_ = new std::atomic<bool>[n];
        hostAt_ = new std::atomic<long long>[n];
        told_ = new std::atomic<float>[n];
        for (size_t i = 0; i < n; ++i)
        {
            held_[i].store(false); release_[i].store(false); hostMoved_[i].store(false);
            hostAt_[i].store(0); told_[i].store(-1.0e9f);
        }
        lastPos_.assign(n, -1.0f);
        textHash_.assign(n, 0u);

        // The engine reads <data dir>/roms/; 1.x's data dir was <plugin folder>/xenia (MODULE_SUBDIR). (Not mpc::contentPath(): a
        // function-local static std::string.)
        const std::string folder = mpc::detail::computePluginFolder();
        dataDir_ = folder + "/xenia";
        eng_ = mpc_engine();
        inst_ = eng_->create(dataDir_.c_str());
        addProgram("Current sound", {});
        for (int i = 0; i < kFavSlots; ++i) addProgram("--", {});
        favFile_ = folder + "/xenia-favourites.dat";
        loadFavs();
        worker_ = std::thread(&XeniaPlugin::workerMain, this);
    }

    ~XeniaPlugin() override
    {
        quit_.store(true);
        if (worker_.joinable()) worker_.join();
        if (inst_) eng_->destroy(inst_);
        delete[] held_;
        delete[] release_;
        delete[] hostMoved_;
        delete[] hostAt_;
        delete[] told_;
    }

    void prepare(double sampleRate, int) override
    {
        step_ = sampleRate > 1000 ? (double)kEngineRate / sampleRate : 1.0;
        frac_ = 0;
        have_ = 0;
    }

    void reset() override
    {
        store(kUiParam, 0.0f);
        notifyUi_.store(true);
    }

    // ---- audio ----------------------------------------------------------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        if (!inst_) return;
        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        int frame = 0, mi = 0;
        while (frame < ctx.numFrames)
        {
            while (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset <= frame)
            {
                const mpc::MidiEvent& e = ctx.midi[mi++];
                eng_->midi(inst_, e.data, e.size);
            }
            int end = ctx.numFrames;
            if (mi < ctx.numMidiEvents) end = std::min(std::max(ctx.midi[mi].sampleOffset, frame + 1), ctx.numFrames);
            while (frame < end)
            {
                const int n = std::min(end - frame, kChunk);
                renderOut(L + frame, R ? R + frame : nullptr, n);
                frame += n;
            }
        }
    }

    // ---- parameters -----------------------------------------------------------------------------------------------------------
    void parameterChanged(int index, float plain) override
    {
        if (index < 0 || index >= kNumParams || !inst_) return;
        if (t_pushing == index) return;          // the host echoing what the worker just told it
        const PSpec& s = g_pspecs[index];
        if (s.flags & (kUi | kReserved)) return;
        if (s.flags & kLocal) { localChanged(index, plain); return; }
        hostAt_[index].store(nowMs());
        if (loading_.load()) return;             // an MPC chunk is restoring values: the engine state follows as extra state
        if ((s.flags & kChoice) && get(kUiParam) > 0.5f)   // a pick from the open drawn list closes it
        {
            store(kUiParam, 0.0f);
            notifyUi_.store(true);
        }
        if ((s.flags & kText) && !(s.flags & kTile)) return;   // a readout's counter: nothing to send
        if (s.flags & kMomentary)
        {
            if (plain < 0.5f) { held_[index].store(false); if (s.flags & kTile) hostMoved_[index].store(true); return; }
            if (held_[index].exchange(true)) return;
            engineSet(s.key, "1");               // the engine's own verbs (preset_next wraps, tiles pick, PANIC, INIT PATCH ...)
            if (s.flags & kStep) hostMoved_[s.stepTarget].store(true);
            if (s.flags & kTile) hostMoved_[index].store(true);   // the tile shows "<key>_on", read back by the worker
            else release_[index].store(true);
            return;
        }
        // whole numbers and options: settle toward the move, then send
        const float span = s.max - s.min;
        float v = plain;
        if (span > 0)
        {
            const float pos = getNormalised(index) * span;
            const float cur = told_[index] > -1.0e8f ? told_[index] - s.min : pos;
            float steps = settle(pos, cur, lastPos_[(size_t)index]);
            // The data wheel on PRESET moves the host value by more than one step per click on a long range: a small off-step move
            // there is one step in its direction (a drag or Q-Link still lands where it points).
            if (!std::strcmp(s.key, "preset") && std::fabs(pos - std::round(pos)) > 0.001f && std::fabs(pos - cur) < 3.0f && pos != cur)
                steps = cur + (pos > cur ? 1.0f : -1.0f);
            lastPos_[(size_t)index] = pos;
            steps = std::min(std::max(steps, 0.0f), span);
            v = s.min + steps;
            // Where it landed reaches the host from the worker ~10 ms later: the host's own value is left as it set it until then,
            // so a set reads back unchanged (host_sim's sweep checks that).
            if (std::fabs(getNormalised(index) * span - steps) > 0.001f) hostMoved_[index].store(true);
        }
        if (index == 0)   // debounced (see the header comment)
        {
            pend0Value_.store(v);
            pend0At_.store(nowMs());
            return;
        }
        told_[index] = v;
        sendValue(index, v);
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        (void)plain;
        if (index < 0 || index >= kNumParams) return false;
        const PSpec& s = g_pspecs[index];
        char buf[128];
        if (s.flags & kText) { out = engineGet(s.key, buf, sizeof buf) ? buf : ""; return true; }   // readouts and tile names
        if (s.flags & (kMomentary | kUi | kReserved)) { out = ""; return true; }
        return false;
    }

    // ---- state ----------------------------------------------------------------------------------------------------------------
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const std::string s = currentState();
        out.assign(s.begin(), s.end());
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        if (!data || !size) return;
        ignoreProgUntil_.store(nowMs() + 2000);   // the host re-picks the saved program after a project load: the project's sound wins
        applyState(std::string(reinterpret_cast<const char*>(data), strnlen(reinterpret_cast<const char*>(data), size)));
    }

    // A project saved by the 1.x wrapper build: its chunk is the engine's state JSON, NUL-terminated. Called from the wrapped effSetChunk.
    bool loadLegacy(const char* data, size_t size)
    {
        if (!data || !size) return false;
        const std::string s(data, strnlen(data, size));
        if (!isState(s)) return false;
        ignoreProgUntil_.store(nowMs() + 2000);
        applyState(s);
        return true;
    }

    std::atomic<bool> loading_{false};   // set around an MPC chunk load (wrapped effSetChunk)
    void requestResync() { resync_.store(true); }

private:
    // 44.1 kHz engine -> host rate. At 44.1 kHz the engine renders straight into the output.
    void renderOut(float* L, float* R, int n)
    {
        if (std::fabs(step_ - 1.0) < 1e-9)
        {
            eng_->render(inst_, pcm_, n);
            for (int i = 0; i < n; ++i)
            {
                L[i] = pcm_[2 * i] * (1.0f / 32768.0f);
                if (R) R[i] = pcm_[2 * i + 1] * (1.0f / 32768.0f);
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            while (have_ < 2 || frac_ >= 1.0)   // keep two source frames: [0] at frac_ = 0, [1] at 1
            {
                if (have_ >= 2) { src_[0] = src_[2]; src_[1] = src_[3]; --have_; frac_ -= 1.0; }
                if (srcPos_ >= srcLen_) { eng_->render(inst_, pcm_, kChunk); srcPos_ = 0; srcLen_ = kChunk; }
                src_[have_ * 2] = pcm_[srcPos_ * 2] * (1.0f / 32768.0f);
                src_[have_ * 2 + 1] = pcm_[srcPos_ * 2 + 1] * (1.0f / 32768.0f);
                ++srcPos_;
                ++have_;
            }
            const float f = (float)frac_;
            L[i] = src_[0] + (src_[2] - src_[0]) * f;
            if (R) R[i] = src_[1] + (src_[3] - src_[1]) * f;
            frac_ += step_;
        }
    }

    void engineSet(const char* key, const char* val) { eng_->set_param(inst_, key, val); }

    bool engineGet(const char* key, char* buf, int n)
    {
        const int r = eng_->get_param(inst_, key, buf, n);
        if (r <= 0) { buf[0] = 0; return false; }
        buf[r < n ? r : n - 1] = 0;
        return true;
    }

    bool engineReady()
    {
        char b[8];
        return engineGet("ready", b, sizeof b) && std::atoi(b) != 0;
    }

    std::string engineState()
    {
        std::vector<char> buf((size_t)kStateMax);
        return inst_ && engineGet("state", buf.data(), kStateMax) ? std::string(buf.data()) : std::string();
    }

    // What a save stores: the engine's state once it has booted; before that the state the project gave us (it would otherwise lose
    // the edited sound, which the engine only reports once it is running).
    std::string currentState()
    {
        {
            std::lock_guard<std::mutex> g(stateMu_);
            if (!pendingState_.empty())
            {
                if (!engineReady()) return pendingState_;
                pendingState_.clear();
            }
        }
        return engineState();
    }

    void applyState(const std::string& s)
    {
        if (!inst_ || !isState(s)) return;
        engineSet("state", s.c_str());
        if (!engineReady()) { std::lock_guard<std::mutex> g(stateMu_); pendingState_ = s; }
        resync_.store(true);
    }

    void sendValue(int index, float v)
    {
        const PSpec& s = g_pspecs[index];
        const int iv = (int)std::lround(v);
        if ((s.flags & kChoice) && isPlayTarget(s.key))   // the engine matches the PLAY knob target by its label
        {
            if (iv >= 0 && iv < s.nchoices) engineSet(s.key, s.choices[iv]);
            return;
        }
        char buf[24];
        std::snprintf(buf, sizeof buf, "%d", iv);
        engineSet(s.key, buf);
    }

    // Set a parameter's value without calling parameterChanged (so it is not written back to the engine).
    void store(int i, float plain) { values_[(size_t)i]->store(param(i).toNormalised(plain), std::memory_order_relaxed); }

    // Where a whole-number / option param lands, in steps from its minimum (the 1.x wrapper's rule): a value on a step is a click,
    // a preset or automation; between steps it is a Q-Link / wheel / drag move, rounded toward the way it moves.
    static float settle(float pos, float cur, float last)
    {
        if (std::fabs(pos - std::round(pos)) <= 0.001f) return std::round(pos);
        const float dir = (last >= 0 && std::fabs(pos - last) < 0.5f) ? pos - last : pos - cur;
        if (dir == 0) return std::round(cur);
        return dir > 0 ? std::ceil(pos - 0.001f) : std::floor(pos + 0.001f);
    }

    // ---- worker: debounced parameter 0, momentary releases, everything the engine changes by itself -----------------------------
    void pushValue(int i, float plain)
    {
        told_[i] = plain;
        store(i, plain);
        t_pushing = i;
        notifyHostParameterChanged(i);
        t_pushing = -1;
    }

    void workerMain()
    {
        while (!quit_.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            tickOnce(false);
        }
    }

    // One worker pass. force = the offline test: apply a pending parameter 0 now and read back everything.
    void tickOnce(bool force)
    {
        if (!inst_) return;
        const long long now = nowMs();
        ++tick_;
        const long long p0 = pend0At_.load();
        if (p0 && (force || now - p0 >= 40))
        {
            pend0At_.store(0);
            const float v = pend0Value_.load();
            char buf[24];
            if (engineGet(g_pspecs[0].key, buf, sizeof buf) && std::atoi(buf) != (int)std::lround(v)) sendValue(0, v);
            told_[0] = -1.0e9f;   // read back below
            hostMoved_[0].store(true);
        }
        for (int i = 0; i < kNumParams; ++i)
            if (release_[i].exchange(false)) { held_[i].store(false); pushValue(i, 0.0f); }
        {   // a favourite picked in the MPC's PRESET menu (not the re-pick that follows a project load: the project's sound wins)
            const int prog = currentProgram();
            if (prog != lastProgram_)
            {
                lastProgram_ = prog;
                if (prog > 0 && now >= ignoreProgUntil_.load()) loadFav(prog - 1);
            }
        }
        if (favDirty_.exchange(false)) { saveFavs(); wantDisplay_ = true; }   // new names in the PRESET menu
        if (notifyUi_.exchange(false)) { t_pushing = kUiParam; notifyHostParameterChanged(kUiParam); t_pushing = -1; }
        if (resync_.exchange(false))
        {
            for (int i = 0; i < kNumParams; ++i) { told_[i].store(-1.0e9f); hostMoved_[i].store(true); }
            std::fill(textHash_.begin(), textHash_.end(), 0u);
        }

        // display_rev moves on anything that changes many values (sound, bank, state, PLAY target, the BANKS page): then every value and
        // tile is read back
        char buf[128];
        bool full = force;
        if (engineGet("display_rev", buf, sizeof buf))
        {
            const uint32_t r = fnv(buf) | 1u;
            if (r != lastRev_) { lastRev_ = r; full = true; }
        }
        int changed = 0;
        for (int i = 0; i < kNumEngineParams; ++i)
        {
            const PSpec& s = g_pspecs[i];
            const bool moved = hostMoved_[i].exchange(false);
            if (!(moved || full)) continue;
            if (s.flags & kTile)
            {
                char k2[64];
                std::snprintf(k2, sizeof k2, "%s_on", s.key);
                const float on = engineGet(k2, buf, sizeof buf) && std::atoi(buf) ? 1.0f : 0.0f;
                if (moved || on != told_[i]) { pushValue(i, on); if (!moved) ++changed; }
                continue;
            }
            if (!(s.flags & kPolled)) continue;
            if (i == 0 && pend0At_.load()) { if (moved) hostMoved_[0].store(true); continue; }
            // while the host is still moving it, don't send the engine's value back under the finger; read it back once it has rested
            if (!force && now - hostAt_[i].load() < 300) { if (moved || full) hostMoved_[i].store(true); continue; }
            if (!engineGet(s.key, buf, sizeof buf)) continue;
            float v;
            if ((s.flags & kChoice) && isPlayTarget(s.key))   // the engine answers with the label
            {
                v = 0;
                for (int k = 0; k < s.nchoices; ++k) if (!std::strcmp(buf, s.choices[k])) v = (float)k;
            }
            else v = (float)std::atof(buf);
            v = std::min(std::max(v, s.min), s.max);
            if (moved || v != told_[i]) { pushValue(i, v); if (!moved) ++changed; }
        }

        // text readouts: every 100 ms; a changed text bumps the counter (the host re-reads text when the value moves)
        bool textChanged = false;
        if (force || full || tick_ % 10 == 0)
            for (int i = 0; i < kNumEngineParams; ++i)
            {
                const PSpec& s = g_pspecs[i];
                if (!(s.flags & kText)) continue;
                const uint32_t h = engineGet(s.key, buf, sizeof buf) ? fnv(buf) | 1u : 1u;
                if (h == textHash_[(size_t)i]) continue;
                textHash_[(size_t)i] = h;
                textChanged = true;
                if (s.flags & kTile) continue;   // a tile's value is its lit state
                counter_ = (counter_ + 1) % 1024;
                pushValue(i, (float)counter_);
            }
        // Many values at once or new text: one display update, at most every 100 ms. Never periodically: it re-lists programs and
        // closes the PRESET menu.
        if (textChanged || changed > 8) wantDisplay_ = true;
        if (wantDisplay_ && (force || now - lastDisplay_ >= 100))
        {
            wantDisplay_ = false;
            lastDisplay_ = now;
            requestDisplayUpdate();
        }
    }

    // ---- favourites, random (kLocal) --------------------------------------------------------------------------------------------
    struct Fav { bool used = false; std::string name, state; };

    // "XNF1", slot count, then per slot: used (1 byte), name length + name, state length + state (u32 little endian lengths)
    void loadFavs()
    {
        std::lock_guard<std::mutex> g(favMu_);
        favs_.assign(kFavSlots, Fav());
        std::ifstream f(favFile_, std::ios::binary);
        char magic[4] = {};
        uint32_t n = 0;
        if (f.read(magic, 4) && !std::memcmp(magic, "XNF1", 4) && f.read(reinterpret_cast<char*>(&n), 4))
            for (uint32_t i = 0; i < n && i < (uint32_t)kFavSlots; ++i)
            {
                char used = 0;
                uint32_t ln = 0, ls = 0;
                if (!f.read(&used, 1) || !f.read(reinterpret_cast<char*>(&ln), 4) || ln > 256) break;
                std::string name(ln, '\0');
                if (ln && !f.read(&name[0], ln)) break;
                if (!f.read(reinterpret_cast<char*>(&ls), 4) || ls > (uint32_t)kStateMax) break;
                std::string st(ls, '\0');
                if (ls && !f.read(&st[0], ls)) break;
                favs_[i].used = used != 0 && isState(st);
                favs_[i].name = name;
                favs_[i].state = st;
            }
        for (int i = 0; i < kFavSlots; ++i) nameProgram(i);
    }

    void saveFavs()   // worker thread: a snapshot under the lock, then the file (.tmp + rename)
    {
        std::vector<Fav> snap;
        { std::lock_guard<std::mutex> g(favMu_); snap = favs_; }
        const std::string tmp = favFile_ + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return;
            const uint32_t n = (uint32_t)snap.size();
            f.write("XNF1", 4);
            f.write(reinterpret_cast<const char*>(&n), 4);
            for (const Fav& v : snap)
            {
                const char used = v.used ? 1 : 0;
                const uint32_t ln = (uint32_t)v.name.size(), ls = (uint32_t)v.state.size();
                f.write(&used, 1);
                f.write(reinterpret_cast<const char*>(&ln), 4);
                f.write(v.name.data(), ln);
                f.write(reinterpret_cast<const char*>(&ls), 4);
                f.write(v.state.data(), ls);
            }
        }
        std::rename(tmp.c_str(), favFile_.c_str());
    }

    void nameProgram(int i)   // favMu_ held
    {
        programs_[(size_t)i + 1].name = std::to_string(i + 1) + " " + (favs_[(size_t)i].used ? favs_[(size_t)i].name : std::string("--"));
    }

    void loadFav(int slot)   // worker thread
    {
        std::string st;
        { std::lock_guard<std::mutex> g(favMu_); if (slot >= 0 && slot < kFavSlots && favs_[(size_t)slot].used) st = favs_[(size_t)slot].state; }
        if (!st.empty()) applyState(st);
    }

    void localChanged(int index, float plain)
    {
        if (plain < 0.5f) { held_[index].store(false); return; }
        if (held_[index].exchange(true)) return;
        release_[index].store(true);   // a momentary: back to 0 on the next worker pass
        if (index == kRandom)
        {
            engineSet("random_sound", "1");
            for (int i = 0; i < kNumEngineParams; ++i)
                if (!std::strcmp(g_pspecs[i].key, "preset") || !std::strcmp(g_pspecs[i].key, "bank_index")) { told_[i].store(-1.0e9f); hostMoved_[i].store(true); }
            return;
        }
        if (index == kFavAdd)
        {
            if (!engineReady()) return;
            Fav v;
            v.state = engineState();
            char b[128];
            if (engineGet("preset_name", b, sizeof b))   // "038 Name": the PRESET menu shows the slot number, keep the name
            {
                const char* p = b;
                while (*p >= '0' && *p <= '9') ++p;
                while (*p == ' ') ++p;
                v.name = *p ? p : b;
            }
            while (!v.name.empty() && v.name.back() == ' ') v.name.pop_back();
            if (v.name.empty()) v.name = "Favourite";
            if (!isState(v.state)) return;
            v.used = true;
            int slot = -1;
            {
                std::lock_guard<std::mutex> g(favMu_);
                for (int i = 0; i < kFavSlots && slot < 0; ++i) if (!favs_[(size_t)i].used) slot = i;
                if (slot >= 0) { favs_[(size_t)slot] = v; nameProgram(slot); }
            }
            if (slot >= 0) favDirty_.store(true);
            return;
        }
        if (index == kFavDel)   // the favourite that is picked now
        {
            const int prog = currentProgram();
            if (prog < 1 || prog > kFavSlots) return;
            bool had;
            {
                std::lock_guard<std::mutex> g(favMu_);
                had = favs_[(size_t)prog - 1].used;
                if (had) { favs_[(size_t)prog - 1] = Fav(); nameProgram(prog - 1); }
            }
            if (had) favDirty_.store(true);
        }
    }

public:
#ifdef XENIA_TEST
    void dbgTick() { tickOnce(true); }
    bool dbgReady() { return engineReady(); }
    std::string dbgEngine(const char* key)
    {
        char b[kStateMax];
        return engineGet(key, b, sizeof b) ? std::string(b) : std::string();
    }
    void dbgEngineSet(const char* key, const char* v) { engineSet(key, v); }
#endif

private:
    const mpc_engine_t* eng_ = nullptr;
    void* inst_ = nullptr;
    std::string dataDir_, favFile_;
    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> resync_{true}, notifyUi_{false};
    std::atomic<long long> pend0At_{0};
    std::atomic<float> pend0Value_{0.0f};
    std::atomic<bool>* held_ = nullptr;
    std::atomic<bool>* release_ = nullptr;
    std::atomic<bool>* hostMoved_ = nullptr;   // read this one back and tell the host on the next pass
    std::atomic<long long>* hostAt_ = nullptr; // when the host last moved it (ms), per parameter
    std::atomic<float>* told_ = nullptr;       // the value the host was last told (or sent), per parameter
    std::vector<float> lastPos_;               // the host's last position, for settle()
    std::vector<uint32_t> textHash_;
    std::mutex stateMu_;
    std::string pendingState_;                 // a project's state given before the engine booted
    std::mutex favMu_;
    std::vector<Fav> favs_;
    std::atomic<bool> favDirty_{false};
    std::atomic<long long> ignoreProgUntil_{0};
    int lastProgram_ = 0;   // worker thread
    // audio thread: resampler
    double step_ = 1.0, frac_ = 0;
    int have_ = 0, srcPos_ = 0, srcLen_ = 0;
    float src_[4] = {0, 0, 0, 0};
    int16_t pcm_[kChunk * 2] = {0};
    uint32_t lastRev_ = 0;
    int counter_ = 0;
    long tick_ = 0;
    bool wantDisplay_ = false;
    long long lastDisplay_ = 0;
};

// The same entry as MPC_PLUGIN_ENTRY, plus a wrapped effSetChunk: an "MPCP" chunk loads with engine writes held back (the engine
// state in its extra part is the truth); a 1.x chunk (the engine's state JSON) goes to the engine as before.
namespace {
VstIntPtr (*g_origDispatcher)(AEffect*, VstInt32, VstInt32, VstIntPtr, void*, float) = nullptr;

VstIntPtr xeniaDispatcher(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    if (opcode == effSetChunk && e->object && ptr && value > 0)
    {
        XeniaPlugin* p = static_cast<XeniaPlugin*>(e->object);
        if (value >= 4 && std::memcmp(ptr, "MPCP", 4) == 0)
        {
            p->loading_.store(true);
            const VstIntPtr r = g_origDispatcher(e, opcode, index, value, ptr, opt);
            p->loading_.store(false);
            p->requestResync();
            return r;
        }
        return p->loadLegacy(static_cast<const char*>(ptr), (size_t)value) ? 1 : 0;
    }
    return g_origDispatcher(e, opcode, index, value, ptr, opt);
}
} // namespace

#ifdef XENIA_TEST   // offline test hooks (tests/host_test.cpp); absent from the release build
extern "C" MPC_EXPORT void xenia_dbg_tick(AEffect* e) { static_cast<XeniaPlugin*>(e->object)->dbgTick(); }
extern "C" MPC_EXPORT int xenia_dbg_ready(AEffect* e) { return static_cast<XeniaPlugin*>(e->object)->dbgReady() ? 1 : 0; }
extern "C" MPC_EXPORT int xenia_dbg_engine(AEffect* e, const char* key, char* out, int n)
{
    const std::string s = static_cast<XeniaPlugin*>(e->object)->dbgEngine(key);
    std::snprintf(out, (size_t)n, "%s", s.c_str());
    return (int)s.size();
}
extern "C" MPC_EXPORT void xenia_dbg_engine_set(AEffect* e, const char* key, const char* v) { static_cast<XeniaPlugin*>(e->object)->dbgEngineSet(key, v); }
#endif

extern "C" MPC_EXPORT AEffect* VSTPluginMain(audioMasterCallback host)
{
    if (host != nullptr && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0) return nullptr;   // host is VST1 only
    AEffect* e = mpc::detail::createEffect(new XeniaPlugin(), host);
    if (e && !g_origDispatcher) g_origDispatcher = e->dispatcher;
    if (e) e->dispatcher = xeniaDispatcher;
    return e;
}
