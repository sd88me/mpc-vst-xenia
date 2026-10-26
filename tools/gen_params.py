#!/usr/bin/env python3
"""Write vst/params.json and src/dsp/xenia_params.h from gearmulator's own parameter table for the Microwave XT
(skin/parameterDescriptions_xt.json, the file Xenia's editor is built from).

The VST parameter index is the position in params.json and saved projects bind to it: the engine's own keys come first,
then the XT's single parameters in table order, and new entries are only ever appended (vst/params.lock, checked by gen_vst.py).

Each XT parameter becomes one VST parameter, named like the table's own name (O1Octave, F1Cutoff, Slot3Amount ...):
  - a value list with a label for every value -> an option list (the wrapper steps it one entry per wheel click / Q-Link event)
  - `signed` (-64..63 on the wire as 0..127) -> a whole number with the offset taken off (kind 0, off 64)
  - `octaves` -> -4..4 (raw 64 + 12 n)
  - everything else -> the raw whole number
xenia_params.h tells the engine how to turn a VST value back into the raw byte the XT's single parameter change wants.
Parameters the XT shares between two meanings (index 70 F1Extra / F1BPOffset, 81 EffectParamA / DelayTime, 159 and 166 LFO rates)
are exposed once, under their first name, as a plain number."""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SRC = os.path.join(ROOT, "skin", "parameterDescriptions_xt.json")
OUT = os.path.join(ROOT, "vst", "params.json")
HDR = os.path.join(ROOT, "src", "dsp", "xenia_params.h")


def load_table():
    def strip(line):
        out, ins, i = [], False, 0
        while i < len(line):
            c = line[i]
            if c == '"':
                ins = not ins
            if not ins and line.startswith("//", i):
                break
            out.append(c)
            i += 1
        return "".join(out)
    t = "\n".join(strip(l) for l in open(SRC).read().split("\n"))
    t = re.sub(r",(\s*[\]}])", r"\1", t)
    d = json.loads(t)
    df = d["parameterdescriptiondefaults"]
    rows, page = [], 0
    for x in d["parameterdescriptions"]:
        page = x.get("page", page)
        y = dict(df)
        y.update(x)
        y["page"] = page
        rows.append(y)
    return rows, d["valuelists"]


rows, VL = load_table()

# readable names; anything not listed is split at capitals ("F1EnvAmount" -> "F1 Env Amount")
NAMES = {
    "O1Octave": "Osc 1 Octave", "O1Semi": "Osc 1 Semitone", "O1Detune": "Osc 1 Detune", "O1BendRange": "Osc 1 Bend Range",
    "O1KeyTrack": "Osc 1 Keytrack", "O1FmAmount": "Osc 1 FM Amount",
    "O2Octave": "Osc 2 Octave", "O2Semi": "Osc 2 Semitone", "O2Detune": "Osc 2 Detune", "O2Sync": "Osc 2 Sync",
    "O2BendRange": "Osc 2 Bend Range", "O2KeyTrack": "Osc 2 Keytrack", "O2Link": "Osc 2 Link",
    "Wave": "Wavetable", "W1StartW": "Wave 1 Start Wave", "W1StartP": "Wave 1 Start Phase", "W1EnvAmount": "Wave 1 Env Amount",
    "W1EnvVelAmount": "Wave 1 Vel Amount", "W1Keytrack": "Wave 1 Keytrack", "W1Limit": "Wave 1 Limit",
    "W2StartW": "Wave 2 Start Wave", "W2StartP": "Wave 2 Start Phase", "W2EnvAmount": "Wave 2 Env Amount",
    "W2EnvVelAmount": "Wave 2 Vel Amount", "W2Keytrack": "Wave 2 Keytrack", "W2Limit": "Wave 2 Limit", "W2Link": "Wave 2 Link",
    "MixW1": "Mix Wave 1", "MixW2": "Mix Wave 2", "MixRingMod": "Mix Ring Mod", "MixNoise": "Mix Noise", "MixExternal": "Mix Ext In",
    "TimeQuant": "Time Quant", "F1Cutoff": "Filter 1 Cutoff", "F1Resonance": "Filter 1 Reso", "F1Type": "Filter 1 Type",
    "F1KeyTrack": "Filter 1 Keytrack", "F1EnvAmount": "Filter 1 Env Amount", "F1EnvVelAmount": "Filter 1 Vel Amount",
    "F1Extra": "Filter 1 Extra", "F2Cutoff": "Filter 2 Cutoff", "F2Type": "Filter 2 Type", "F2KeyTrack": "Filter 2 Keytrack",
    "EffectType": "Effect Type", "EffectParamA": "Effect Param A", "EffectParamB": "Effect Param B", "EffectParamC": "Effect Param C",
    "AmpVolume": "Amp Volume", "AmpVelocity": "Amp Velocity", "AmpKeytrack": "Amp Keytrack", "ChorusEnabled": "Chorus",
    "GlideEnabled": "Glide", "GlideType": "Glide Type", "GlideMode": "Glide Mode", "GlideTime": "Glide Time",
    "ArpMode": "Arp Mode", "ArpTempo": "Arp Tempo", "ArpClock": "Arp Clock", "ArpRange": "Arp Range", "ArpPattern": "Arp Pattern",
    "ArpDirection": "Arp Direction", "ArpNoteOrder": "Arp Note Order", "ArpVelocity": "Arp Velocity", "ArpReset": "Arp Reset",
    "ArpUserPatternLength": "Arp User Length", "DePan": "Pan Spread",
    "F1EnvAttack": "Filter Env Attack", "F1EnvDecay": "Filter Env Decay", "F1EnvSustain": "Filter Env Sustain",
    "F1EnvRelease": "Filter Env Release", "F1EnvTrigger": "Filter Env Trigger",
    "AmpEnvAttack": "Amp Env Attack", "AmpEnvDecay": "Amp Env Decay", "AmpEnvSustain": "Amp Env Sustain",
    "AmpEnvRelease": "Amp Env Release", "AmpEnvTrigger": "Amp Env Trigger",
    "WaveEnvTrigger": "Wave Env Trigger", "WaveKeyOnLoop": "Key On Loop", "WaveKeyOnLoopStart": "Key On Loop Start",
    "WaveKeyOnLoopEnd": "Key On Loop End", "WaveKeyOffLoop": "Key Off Loop", "WaveKeyOffLoopStart": "Key Off Loop Start",
    "WaveKeyOffLoopEnd": "Key Off Loop End", "FreeEnvTrigger": "Free Env Trigger",
    "FreeEnvReleaseTime": "Free Env Rel Time", "FreeEnvReleaseLevel": "Free Env Rel Level",
    "Lfo1Rate": "LFO 1 Rate", "Lfo1Shape": "LFO 1 Shape", "Lfo1Delay": "LFO 1 Delay", "Lfo1Sync": "LFO 1 Sync",
    "Lfo1Symmetry": "LFO 1 Symmetry", "Lfo1Humanize": "LFO 1 Humanize",
    "Lfo2Rate": "LFO 2 Rate", "Lfo2Shape": "LFO 2 Shape", "Lfo2Delay": "LFO 2 Delay", "Lfo2Sync": "LFO 2 Sync",
    "Lfo2Symmetry": "LFO 2 Symmetry", "Lfo2Humanize": "LFO 2 Humanize", "Lfo2Phase": "LFO 2 Phase",
    "ModDelaySource": "Mod Delay Source", "ModDelayTime": "Mod Delay Time",
}


def nice(name):
    if name in NAMES:
        return NAMES[name]
    m = re.match(r"Mod(\d)(Source1|Source2|Type|Parameter)$", name)
    if m:
        return "Mod %s %s" % (m.group(1), {"Source1": "Source 1", "Source2": "Source 2", "Type": "Type", "Parameter": "Param"}[m.group(2)])
    m = re.match(r"Slot(\d+)(Source|Amount|Destination)$", name)
    if m:
        return "Slot %s %s" % (m.group(1), {"Source": "Source", "Amount": "Amount", "Destination": "Dest"}[m.group(2)])
    m = re.match(r"WaveEnv(Time|Level)(\d)$", name)
    if m:
        return "Wave Env %s %s" % (m.group(1), m.group(2))
    m = re.match(r"FreeEnv(Time|Level)(\d)$", name)
    if m:
        return "Free Env %s %s" % (m.group(1), m.group(2))
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", name)


def labels_for(tt, lo, hi):
    """value-list entries for raw values lo..hi that have a label -> (labels, raw values), or None when there is no list.
    A list with gaps (wavetable names, effect types) gives the sparse raw values too."""
    v = VL.get(tt)
    if v is None:
        return None
    if isinstance(v, dict):
        pairs = [(v.get(str(i)), i) for i in range(lo, hi + 1)]
    else:
        pairs = [(v[i] if i < len(v) else None, i) for i in range(lo, hi + 1)]
    pairs = [(s, i) for s, i in pairs if s]
    return [s for s, _ in pairs], [i for _, i in pairs]


# value lists the wrapper should show as a number rather than as 128 option labels
AS_NUMBER = {"unsignedZero", "unsignedOne", "signed", "pbrange", "waveStartWave", "ascii", "midiNote"}
SKIP_TYPES = {"ascii"}
# index shared between two names: keep the first
seen_idx = set()
XT = []     # (key, idx, lo, hi, kind, off)
params = []
keys_in = {}
SEC = {}


def add(p, sec=None):
    params.append(p)
    if sec:
        keys_in.setdefault(sec, []).append(p["key"])


# ---- engine keys first (VST index = order; never reorder)
add(dict(key="bank_index", name="BANK", min=0, max=1, default=0, display="int"), "Browse")
add(dict(key="preset", name="PRESET", min=0, max=99, default=0, display="int"), "Browse")
add(dict(key="bank_name", name="BANK NAME", min=0, max=0, default=0, display="string", poll=True), "Browse")
add(dict(key="preset_name", name="PRESET NAME", min=0, max=0, default=0, display="string", poll=True), "Browse")
add(dict(key="bank_prev", name="BANK -", min=0, max=1, default=0, momentary=True, step_of="bank_index", step_delta=-1), "Browse")
add(dict(key="bank_next", name="BANK +", min=0, max=1, default=0, momentary=True, step_of="bank_index", step_delta=1), "Browse")
add(dict(key="preset_prev", name="PRESET -", min=0, max=1, default=0, momentary=True, step_of="preset", step_delta=-1), "Browse")
add(dict(key="preset_next", name="PRESET +", min=0, max=1, default=0, momentary=True, step_of="preset", step_delta=1), "Browse")
add(dict(key="all_notes_off", name="PANIC", min=0, max=1, default=0, momentary=True), "Browse")
add(dict(key="rom_name", name="ROM", min=0, max=0, default=0, display="string", poll=True), "System")
add(dict(key="loading_status", name="STATUS", min=0, max=0, default=0, display="string", poll=True), "System")
add(dict(key="dsp_clock", name="DSP CLOCK %", min=10, max=100, default=100, display="int"), "System")
add(dict(key="gain", name="GAIN %", min=10, max=400, default=100, display="int"), "System")

SECTIONS = [
    ("Wave Env", ("WaveEnv", "WaveKey")), ("Oscillators", ("O1", "O2")), ("Wavetable", ("Wave", "W1", "W2")), ("Mixer", ("Mix",)), ("Quality", ("Aliasing", "TimeQuant", "Clipping", "Accuracy")),
    ("Filters", ("F1", "F2")), ("Amp", ("Amp",)), ("Effects", ("Effect", "Chorus", "Pan", "DePan")), ("Glide", ("Glide",)),
    ("Arp", ("Arp",)), ("Voice", ("Alloc", "Assignment", "Detune")), ("Free Env", ("FreeEnv",)),
    ("LFO", ("Lfo",)), ("Mod", ("Mod",)), ("Slots", ("Slot",)),
]


def section(name):
    for label, prefixes in SECTIONS:
        if any(name.startswith(p) for p in prefixes):
            return label
    return "Other"


for r in rows:
    if r["page"] != 0:
        continue                      # pages 10-40 are Multi, Global and play parameters: not in v1 (single part only)
    name, idx, lo, hi, tt = r["name"], r["index"], r["min"], r["max"], r["toText"]
    if name == "Version" or name.startswith("PlayParam") or tt in SKIP_TYPES or name.startswith("ArpUserPattern") and name != "ArpUserPatternLength":
        continue
    if idx in seen_idx:
        continue                      # second meaning of a shared index
    seen_idx.add(idx)
    lr = None if tt in AS_NUMBER else labels_for(tt, lo, hi)
    if name in ("Lfo1Rate", "Lfo2Rate", "F1Extra", "EffectParamA") or tt in ("lfoRateSync", "arpUserPatternBits"):
        lr = None
    mapn = None
    if tt == "octaves":
        p = dict(key=name, name=nice(name), min=-4, max=4, default=0, display="int")
        kind, off = 2, 0
    elif lr is not None and len(lr[0]) > 1:
        labels, raws = lr
        dflt = r.get("default", lo)
        di = raws.index(dflt) if dflt in raws else 0
        p = dict(key=name, name=nice(name), options=labels, default=labels[di])
        if raws == list(range(lo, lo + len(raws))):
            kind, off = 1, 0
        else:
            kind, off, mapn = 3, 0, raws
    else:
        off = 64 if tt == "signed" else (-1 if tt == "unsignedOne" else 0)
        dflt = r.get("default", 64 if tt == "signed" and lo == 0 else lo)
        p = dict(key=name, name=nice(name), min=lo - off, max=hi - off, default=dflt - off, display="int")
        kind = 0
    add(p, section(name))
    XT.append((name, idx, lo, hi, kind, off, mapn, int(r.get("default", 64 if tt == "signed" and lo == 0 else lo))))

# ---- BANKS page list tiles (appended after the sound parameters: saved projects bind by index)
for i in range(1, 3):
    add(dict(key="bank_slot_%d" % i, name="BANK %d" % i, min=0, max=1, default=0, momentary=True, display="string", poll=True), "Banks")
for i in range(1, 31):
    add(dict(key="patch_slot_%d" % i, name="PATCH %d" % i, min=0, max=1, default=0, momentary=True, display="string", poll=True), "Banks")
add(dict(key="patch_page_text", name="PAGE", min=0, max=0, default=0, display="string", poll=True), "Banks")
add(dict(key="patch_page_prev", name="PAGE -", min=0, max=1, default=0, momentary=True), "Banks")
add(dict(key="patch_page_next", name="PAGE +", min=0, max=1, default=0, momentary=True), "Banks")
add(dict(key="engine_stats", name="STATS", min=0, max=0, default=0, display="string", poll=True), "System")
add(dict(key="init_patch", name="INIT", min=0, max=1, default=0, momentary=True), "System")

order = ["Browse", "Banks", "System"] + [s for s, _ in SECTIONS] + ["Other"]
sections = [{"label": s, "keys": keys_in[s]} for s in order if s in keys_in]
json.dump({"name": "Xenia", "params": params, "sections": sections}, open(OUT, "w"), indent=1)

with open(HDR, "w") as f:
    f.write("/* generated by tools/gen_params.py from skin/parameterDescriptions_xt.json: do not edit.\n"
            " * One row per XT single parameter: VST key, index in the single dump, raw range, how a VST value maps to the raw byte\n"
            " * (kind 0: raw = value + off, 1: option list, raw = lo + index, 2: octaves, raw = 64 + 12 * value, 3: option list over\n * the sparse raw values in map). */\n"
            "#pragma once\n#include <stdint.h>\n"
            "struct xt_param_t { const char *key; uint16_t idx; int16_t lo, hi; uint8_t kind; int16_t off; const uint8_t *map; uint8_t nmap; int16_t def; };\n")
    for k, idx, lo, hi, kind, off, mapn, dflt in XT:
        if mapn:
            f.write("static const uint8_t xt_map_%s[] = {%s};\n" % (k, ",".join(str(i) for i in mapn)))
    f.write("static const xt_param_t g_xt_params[] = {\n")
    for k, idx, lo, hi, kind, off, mapn, dflt in XT:
        f.write('    {"%s", %d, %d, %d, %d, %d, %s, %d, %d},\n' % (k, idx, lo, hi, kind, off, ("xt_map_" + k) if mapn else "nullptr", len(mapn) if mapn else 0, dflt))
    f.write("};\nstatic const int XT_NUM_PARAMS = %d;\n" % len(XT))
print("params.json: %d params (%d XT single parameters), %d sections" % (len(params), len(XT), len(sections)))
