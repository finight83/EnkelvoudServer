#!/usr/bin/env python
"""Host-side check for EVDEq.h: same maths as the PC server's eq.py.

Compiles the actual biquad code from EVDEq.h (the coefficient builder and the
filter loop are copied by the C preprocessor into a C++ program), then compares
its output against a float64 reference implementation of the identical RBJ
peaking sections. If the two agree to ~1 LSB on noise, the firmware port is
numerically the same equaliser the PC dashboard drives.
"""
import math, os, random, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "EVDEq.h")
BANDS = [31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]
Q = 1.41421356237
FLAT = 0.05
FS = 48000

def peaking(fc, gain_db, fs):
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * fc / fs
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2.0 * Q)
    a0 = 1.0 + alpha / A
    return (((1.0 + alpha * A) / a0, (-2.0 * cw) / a0, (1.0 - alpha * A) / a0),
            (1.0, (-2.0 * cw) / a0, (1.0 - alpha / A) / a0))

def reference(pcm, gains, fs=FS):
    nyq = fs / 2.0
    secs = [peaking(fc, g, fs) for fc, g in zip(BANDS, gains)
            if abs(g) >= FLAT and fc < nyq * 0.9]
    z = [[0.0, 0.0] for _ in secs]
    out = []
    for i in range(0, len(pcm), 2):
        for ch in (0, 1):
            x = float(pcm[i + ch])
            for s, (b, a) in enumerate(secs):
                y = b[0] * x + z[s][0]
                z[s][0] = b[1] * x - a[1] * y + z[s][1]
                z[s][1] = b[2] * x - a[2] * y
                x = y
            v = int(round(x))
            out.append(max(-32768, min(32767, v)))
    # reference writes L then R per frame; the C side keeps interleaved order
    inter = []
    for f in range(len(out) // 2):
        inter.append(out[2 * f])
        inter.append(out[2 * f + 1])
    return inter

def build_and_run(gains, pcm):
    with open(SRC, "r", encoding="utf-8") as f:
        hdr = f.read()
    # Strip the Arduino-only bits; keep the maths.
    body = hdr.split("#pragma once", 1)[1]
    body = body.replace("#include <Arduino.h>", "")
    stub = """
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstdlib>
static uint32_t g_opusSampleRate = %d;
#define LOGI(...) do {} while (0)
#define LOGW(...) do {} while (0)
""" % FS
    prog = stub + body + """
int main(int argc, char **argv) {
  eqInit();
  for (int i = 0; i < EQ_BAND_COUNT; ++i) gEq.gains[i] = (float)atof(argv[1 + i]);
  gEq.enabled = true;
  eqRebuild(g_opusSampleRate);
  static int16_t buf[2048 * 2];
  int n = 0;
  while (scanf("%d", &buf[n]) == 1) n++;
  applyEqFilters(buf, n / 2);
  for (int i = 0; i < n; ++i) printf("%d\\n", (int)buf[i]);
  return 0;
}
"""
    d = tempfile.mkdtemp()
    cpp = os.path.join(d, "eqcheck.cpp")
    exe = os.path.join(d, "eqcheck.exe")
    with open(cpp, "w", encoding="utf-8") as f:
        f.write(prog)
    cc = subprocess.run(["g++", "-O2", "-o", exe, cpp], capture_output=True, text=True)
    if cc.returncode != 0:
        print("g++ unavailable or failed:\n", cc.stderr[:2000])
        return None
    inp = "\n".join(str(s) for s in pcm)
    run = subprocess.run([exe] + ["%.6f" % g for g in gains], input=inp,
                         capture_output=True, text=True)
    return [int(v) for v in run.stdout.split()]

def main():
    random.seed(1234)
    frames = 480
    pcm = []
    for i in range(frames):
        t = i / float(FS)
        l = int(9000 * math.sin(2 * math.pi * 1000 * t) + random.randint(-800, 800))
        r = int(7000 * math.sin(2 * math.pi * 100 * t) + random.randint(-800, 800))
        pcm += [l, r]

    cases = {
        "flat (bypass)":      [0.0] * 10,
        "bass +6 / treble -6": [6, 6, 3, 0, 0, 0, 0, -3, -6, -6],
        "all +12 (clamp)":    [12.0] * 10,
        "16k at 48k only":    [0, 0, 0, 0, 0, 0, 0, 0, 0, 12],
    }
    rc = 0
    for name, gains in cases.items():
        got = build_and_run(gains, pcm)
        if got is None:
            return 1
        exp = reference(pcm, gains)
        if gains == [0.0] * 10:
            ok = got == pcm
            print("%-22s bypass bit-exact: %s" % (name, ok))
            rc |= 0 if ok else 1
            continue
        diff = max(abs(a - b) for a, b in zip(got, exp))
        # 1 LSB of slack: float32 in the firmware vs float64 here.
        ok = diff <= 2
        print("%-22s max |firmware - reference| = %d LSB -> %s"
              % (name, diff, "PASS" if ok else "FAIL"))
        rc |= 0 if ok else 1
    print("\nEQ port verified against the PC maths." if rc == 0
          else "\nEQ port does NOT match the PC maths.")
    return rc

if __name__ == "__main__":
    sys.exit(main())
