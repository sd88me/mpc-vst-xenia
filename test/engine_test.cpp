// Engine-level test through the same interface the wrapper uses (mpc_engine): boots the Microwave XT ROM found in
// <dir>/roms, plays notes, changes bank and preset, moves parameters, checks the state chunk fits the wrapper's buffer and restores.
// usage: xenia_engine_test <data dir with roms/> [seconds of audio per phase]
extern "C" {
#include "engine.h"
}
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("ok   " __VA_ARGS__); else { printf("FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static std::string get(const mpc_engine_t *e, void *i, const char *k) {
    char b[8192] = {0};
    if (e->get_param(i, k, b, sizeof b) <= 0) return "";
    return b;
}

static double render_rms(const mpc_engine_t *e, void *i, double secs, double *peak = nullptr)
{
    int16_t buf[256];
    double sq = 0, pk = 0; long n = 0;
    const int blocks = (int)(secs * 44100 / 128);
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; b++) {
        e->render(i, buf, 128);
        for (int k = 0; k < 256; k++) { double v = buf[k] / 32768.0; sq += v * v; if (fabs(v) > pk) pk = fabs(v); n++; }
        // pace like a host would, or the emulator thread never gets ahead of the ring
        std::this_thread::sleep_until(t0 + std::chrono::microseconds((long)((b + 1) * 128 * 1e6 / 44100)));
    }
    if (peak) *peak = pk;
    return sqrt(sq / n);
}

static bool wait_ready(const mpc_engine_t *e, void *i, int seconds)
{
    for (int w = 0; w < seconds * 10; w++) {
        const std::string s = get(e, i, "loading_status");
        if (s == "Ready" || s.rfind("No ", 0) == 0 || s.find("failed") != std::string::npos) return s == "Ready";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <data dir> [seconds]\n", argv[0]); return 2; }
    const double secs = argc > 2 ? atof(argv[2]) : 1.5;
    const mpc_engine_t *e = mpc_engine();
    CHECK(e != nullptr, "mpc_engine()");
    void *i = e->create(argv[1]);
    CHECK(i != nullptr, "create");

    auto t0 = std::chrono::steady_clock::now();
    const bool up = wait_ready(e, i, 180);
    printf("boot took %.1f s: ROM %s, status \"%s\"\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
           get(e, i, "rom_name").c_str(), get(e, i, "loading_status").c_str());
    CHECK(up, "boot finished");
    if (!up) { e->destroy(i); printf("FAILED\n"); return 1; }
    printf("preset %s \"%s\" bank \"%s\"\n", get(e, i, "preset").c_str(), get(e, i, "preset_name").c_str(), get(e, i, "bank_name").c_str());

    double pk = 0;
    render_rms(e, i, 0.5);
    uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0};
    e->midi(i, on, 3);
    double rms = render_rms(e, i, secs, &pk);
    CHECK(rms > 0.0005, "note 60 -> rms %.4f peak %.3f", rms, pk);
    e->midi(i, off, 3);
    render_rms(e, i, 1.0);
    printf("stats: %s\n", get(e, i, "engine_stats").c_str());

    // names of the ROM's sounds arrive in the background
    for (int w = 0; w < 600 && get(e, i, "patch_slot_30").size() <= 4; w++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(get(e, i, "patch_slot_1").size() > 4, "tile 1 \"%s\", tile 2 \"%s\"", get(e, i, "patch_slot_1").c_str(), get(e, i, "patch_slot_2").c_str());

    // another preset sounds and changes the name
    std::string n0 = get(e, i, "preset_name");
    e->set_param(i, "preset", "17");
    render_rms(e, i, 0.5);
    std::string n1 = get(e, i, "preset_name");
    CHECK(get(e, i, "preset") == "17" && n1 != n0, "preset 17 -> \"%s\" (was \"%s\")", n1.c_str(), n0.c_str());
    e->midi(i, on, 3);
    rms = render_rms(e, i, secs, &pk);
    e->midi(i, off, 3);
    CHECK(rms > 0.0005, "preset 17 note -> rms %.4f peak %.3f", rms, pk);

    // the sound's parameters followed the load: the cutoff read back is the patch's own, and a write sticks
    const std::string cut_patch = get(e, i, "F1Cutoff");
    e->set_param(i, "F1Cutoff", "20");
    render_rms(e, i, 0.2);
    CHECK(get(e, i, "F1Cutoff") == "20" && cut_patch != "", "F1Cutoff %s -> %s", cut_patch.c_str(), get(e, i, "F1Cutoff").c_str());
    e->midi(i, on, 3);
    double rms_dark = render_rms(e, i, secs);
    e->set_param(i, "F1Cutoff", "127");
    render_rms(e, i, 0.2);
    double rms_open = render_rms(e, i, secs);
    e->midi(i, off, 3);
    CHECK(rms_dark != rms_open, "cutoff 20 rms %.4f, cutoff 127 rms %.4f", rms_dark, rms_open);

    // bank change
    e->set_param(i, "bank_index", "1");
    render_rms(e, i, 0.5);
    CHECK(get(e, i, "bank_index") == "1", "bank_index 1 -> \"%s\" / \"%s\"", get(e, i, "bank_name").c_str(), get(e, i, "preset_name").c_str());

    // program change from MIDI
    uint8_t pc[2] = {0xc0, 5};
    e->midi(i, pc, 2);
    render_rms(e, i, 0.5);
    CHECK(get(e, i, "preset") == "5", "program change 5 -> preset %s \"%s\"", get(e, i, "preset").c_str(), get(e, i, "preset_name").c_str());

    // BANKS page tiles
    CHECK(get(e, i, "bank_slot_1") == "BANK A" && get(e, i, "bank_slot_2_on") == "1" && get(e, i, "bank_slot_1_on") == "0", "bank tiles %s/%s",
          get(e, i, "bank_slot_1").c_str(), get(e, i, "bank_slot_2").c_str());
    e->set_param(i, "patch_slot_3", "1");
    render_rms(e, i, 0.5);
    CHECK(get(e, i, "preset") == "2" && get(e, i, "patch_slot_3_on") == "1", "patch tile 3 -> preset %s \"%s\"", get(e, i, "preset").c_str(), get(e, i, "patch_slot_3").c_str());
    e->set_param(i, "patch_page_next", "1");
    CHECK(get(e, i, "patch_page_text") == "PAGE 2 / 4" && get(e, i, "patch_slot_1").rfind("031", 0) == 0, "page 2: %s, first tile \"%s\"", get(e, i, "patch_page_text").c_str(), get(e, i, "patch_slot_1").c_str());
    e->set_param(i, "bank_slot_1", "1");
    render_rms(e, i, 0.5);
    CHECK(get(e, i, "bank_index") == "0", "bank tile 1 -> bank %s", get(e, i, "bank_index").c_str());

    // the stepper arrows send preset_next/preset_prev/bank_next/bank_prev (1 on press)
    e->set_param(i, "preset", "98");
    render_rms(e, i, 0.3);
    e->set_param(i, "preset_next", "1");
    render_rms(e, i, 0.3);
    const std::string pn1 = get(e, i, "preset_name");
    e->set_param(i, "preset_next", "1");
    render_rms(e, i, 0.3);
    CHECK(get(e, i, "preset") == "0" && get(e, i, "preset_name") != pn1, "preset_next 98 -> 99 -> 0 (wrap) \"%s\"", get(e, i, "preset_name").c_str());
    e->set_param(i, "preset_prev", "1");
    render_rms(e, i, 0.3);
    CHECK(get(e, i, "preset") == "99", "preset_prev 0 -> %s", get(e, i, "preset").c_str());
    const std::string b0 = get(e, i, "bank_index");
    e->set_param(i, "bank_next", "1");
    render_rms(e, i, 0.3);
    CHECK(get(e, i, "bank_index") != b0, "bank_next %s -> %s", b0.c_str(), get(e, i, "bank_index").c_str());
    e->midi(i, on, 3);
    rms = render_rms(e, i, secs, &pk);
    e->midi(i, off, 3);
    CHECK(rms > 0.0005, "sound after stepping rms %.4f", rms);

    // INIT PATCH: every exposed parameter at its table default
    e->set_param(i, "init_patch", "1");
    render_rms(e, i, 1.0);
    CHECK(get(e, i, "F1Cutoff") == "127", "INIT -> F1Cutoff %s", get(e, i, "F1Cutoff").c_str());

    // state
    e->set_param(i, "preset", "9");
    render_rms(e, i, 0.5);
    e->set_param(i, "F1Resonance", "77");
    render_rms(e, i, 0.3);
    const std::string res0 = get(e, i, "F1Resonance"), bank0 = get(e, i, "bank_index"), pre0 = get(e, i, "preset");
    std::string st = get(e, i, "state");
    CHECK(!st.empty() && st.size() < 8000, "state %zu bytes (wrapper buffer 8192)", st.size());
    e->destroy(i);
    i = e->create(argv[1]);
    e->set_param(i, "state", st.c_str());      // while booting
    CHECK(wait_ready(e, i, 180), "second boot");
    render_rms(e, i, 1.0);
    CHECK(get(e, i, "F1Resonance") == res0 && get(e, i, "bank_index") == bank0 && get(e, i, "preset") == pre0, "state restored F1Resonance %s bank %s preset %s",
          get(e, i, "F1Resonance").c_str(), get(e, i, "bank_index").c_str(), get(e, i, "preset").c_str());
    e->destroy(i);
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails ? 1 : 0;
}
