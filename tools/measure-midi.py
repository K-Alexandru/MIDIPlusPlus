"""How human is a MIDI file? Onset asynchrony, grid alignment, velocity spread."""
import os
import statistics
import struct
import sys


def read_var(data, i):
    v = 0
    while True:
        b = data[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def parse(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"MThd"
    fmt, ntrk, div = struct.unpack(">HHH", data[8:14])
    pos = 8 + struct.unpack(">I", data[4:8])[0]
    notes, tempos, pedal = [], [(0, 500000)], 0
    for _ in range(ntrk):
        if data[pos:pos + 4] != b"MTrk":
            break
        length = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        i, end, tick, status = pos + 8, pos + 8 + length, 0, 0
        pos = end
        down = {}
        while i < end:
            dt, i = read_var(data, i)
            tick += dt
            b = data[i]
            if b & 0x80:
                status = b
                i += 1
            if status == 0xFF:
                kind = data[i]
                n, i = read_var(data, i + 1)
                if kind == 0x51:
                    tempos.append((tick, int.from_bytes(data[i:i + 3], "big")))
                i += n
            elif status in (0xF0, 0xF7):
                n, i = read_var(data, i)
                i += n
            else:
                hi = status & 0xF0
                if hi in (0xC0, 0xD0):
                    i += 1
                else:
                    a, v = data[i], data[i + 1]
                    i += 2
                    if hi == 0x90 and v > 0:
                        down[a] = (tick, v)
                    elif hi == 0x80 or (hi == 0x90 and v == 0):
                        if a in down:
                            t0, vel = down.pop(a)
                            notes.append((t0, tick, a, vel))
                    elif hi == 0xB0 and a == 64:
                        pedal += 1
    return div, sorted(tempos), sorted(notes), pedal


def to_ms(tick, div, tempos):
    ms, last_tick, tempo = 0.0, 0, 500000
    for t, us in tempos:
        if t >= tick:
            break
        ms += (t - last_tick) * tempo / div / 1000.0
        last_tick, tempo = t, us
    return ms + (tick - last_tick) * tempo / div / 1000.0


def measure(path):
    div, tempos, notes, pedal = parse(path)
    if div & 0x8000 or not notes:
        return None
    on = [(to_ms(n[0], div, tempos), n[0], n[3]) for n in notes]
    # Chords: onsets within 50 ms of the chord's first note.
    chords, cur = [], [on[0]]
    for o in on[1:]:
        if o[0] - cur[0][0] <= 50.0:
            cur.append(o)
        else:
            chords.append(cur)
            cur = [o]
    chords.append(cur)
    multi = [c for c in chords if len(c) > 1]
    same_tick = sum(1 for c in multi if len({o[1] for o in c}) == 1)
    spreads = [c[-1][0] - c[0][0] for c in multi]
    grid = div / 12.0
    on_grid = sum(1 for o in on if abs(o[1] / grid - round(o[1] / grid)) < 1e-9)
    ticks = sorted({o[1] for o in on})
    gaps = [to_ms(b, div, tempos) - to_ms(a, div, tempos) for a, b in zip(ticks, ticks[1:])]
    vels = [o[2] for o in on]
    lens = [to_ms(n[1], div, tempos) - to_ms(n[0], div, tempos) for n in notes]
    return {
        "notes": len(notes), "ppq": div, "tempos": len(tempos) - 1, "pedal": pedal,
        "chords": len(multi),
        "same_tick_pct": 100.0 * same_tick / len(multi) if multi else 0.0,
        "spread_med_ms": statistics.median(spreads) if spreads else 0.0,
        "on_grid_pct": 100.0 * on_grid / len(on),
        "gaps_lt1ms_pct": 100.0 * sum(1 for g in gaps if g < 1.0) / len(gaps) if gaps else 0.0,
        "gaps_lt3ms_pct": 100.0 * sum(1 for g in gaps if g < 3.0) / len(gaps) if gaps else 0.0,
        "vel_distinct": len(set(vels)), "vel_sd": statistics.pstdev(vels),
        "vel_levels32": len({v * 32 // 128 for v in vels}),
        "len_distinct_pct": 100.0 * len({round(x) for x in lens}) / len(lens),
    }


folder = sys.argv[1]
for name in sorted(os.listdir(folder)):
    if not name.lower().endswith((".mid", ".midi")):
        continue
    try:
        m = measure(os.path.join(folder, name))
    except Exception as ex:  # a malformed file should not stop the rest
        print(f"{name}: {type(ex).__name__} {ex}")
        continue
    if not m:
        print(f"{name}: no notes or SMPTE timing")
        continue
    print(name)
    print("  notes {notes}  ppq {ppq}  tempo changes {tempos}  pedal events {pedal}".format(**m))
    print("  chords {chords}  same-tick {same_tick_pct:.0f}%  median spread {spread_med_ms:.1f} ms".format(**m))
    print("  onsets on 1/48 grid {on_grid_pct:.0f}%  distinct onsets <1 ms apart {gaps_lt1ms_pct:.1f}%  <3 ms {gaps_lt3ms_pct:.1f}%".format(**m))
    print("  velocities {vel_distinct} distinct, sd {vel_sd:.1f}, {vel_levels32} of 32 game levels  distinct lengths {len_distinct_pct:.0f}%".format(**m))
