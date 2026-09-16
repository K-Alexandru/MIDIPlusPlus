#pragma once

// SheetPage: the coloured sheet as a page that edits itself.
//
// midi-converter is a web page: the settings sit beside the sheet and every
// change redraws it. The shell has no sheet of its own, so "Open coloured
// sheet" writes this page and hands it to the browser. It carries the notes,
// the key mapping, the tempo and meter marks and the current style, and the
// script below is sheet::Style translated once more, into JavaScript, so the
// page redraws as the app would. The C++ rendering is embedded as well, and
// the page checks its own first render against it, so a drift between the two
// translations is reported rather than silent. tests/sheet-page-parity.js
// holds the same check over the fixtures ShellTests writes.
//
// Header only, like SheetExport.hpp, and free of project references.

#include "SheetExport.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace sheet {

// A run transposed on its own, in seconds, both ends inclusive.
struct Region { double from = 0; double to = 0; int semitones = 0; };

struct PageInput {
    std::string title;                             // the MIDI file's stem
    std::vector<TimedNote> notes;                  // before any region is applied
    std::map<std::string, std::string> mapping;
    std::vector<TempoMark> tempos;
    std::vector<MeterMark> meters;
    StyleOptions options;
    std::vector<Region> regions;
};

namespace detail {

inline std::string JsonString(const std::string& text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        // "</script>" inside the data would end the script element; a
        // solidus may always be escaped in JSON.
        case '/': out += "\\/"; break;
        default:
            if (c < 0x20) { char buffer[8]; snprintf(buffer, sizeof(buffer), "\\u%04x", c); out += buffer; }
            else out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

inline std::string JsonNumber(double value) {
    if (!(value == value) || value > 1e300 || value < -1e300) return "0";
    char buffer[40]; snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

inline std::string JsonOptions(const StyleOptions& o) {
    std::string j = "{";
    const auto field = [&](const char* name, const std::string& value) { if (j.size() > 1) j += ","; j += std::string("\"") + name + "\":" + value; };
    const auto flag = [&](bool b) { return std::string(b ? "true" : "false"); };
    field("quantizeMs", JsonNumber(o.quantizeMs));
    field("sequentialQuantize", flag(o.sequentialQuantize));
    field("curlyQuantizes", flag(o.curlyQuantizes));
    field("classicChordOrder", flag(o.classicChordOrder));
    field("shifts", std::to_string(static_cast<int>(o.shifts)));
    field("outOfRangePlace", std::to_string(static_cast<int>(o.outOfRangePlace)));
    field("showOutOfRange", flag(o.showOutOfRange));
    field("outOfRangeMarks", flag(o.outOfRangeMarks));
    field("outOfRangeSeparator", JsonString(o.outOfRangeSeparator));
    field("tempoMarks", flag(o.tempoMarks));
    field("bpmChanges", flag(o.bpmChanges));
    field("bpmStyle", std::to_string(static_cast<int>(o.bpmStyle)));
    field("minSpeedChange", std::to_string(o.minSpeedChange));
    field("breaks", std::to_string(static_cast<int>(o.breaks)));
    field("beats", std::to_string(o.beats));
    field("missingBpm", JsonNumber(o.missingBpm));
    field("transpose", std::to_string(o.transpose));
    field("autoTranspose", flag(o.autoTranspose));
    field("resilience", std::to_string(o.resilience));
    return j + "}";
}

// The page's data: everything the script needs to draw the sheet again, and
// the app's own text of it for the parity check.
inline std::string PageJson(const PageInput& in, const std::string& expectedText) {
    std::string j = "{\"title\":" + JsonString(in.title) + ",\"notes\":[";
    for (size_t i = 0; i < in.notes.size(); ++i)
        j += (i ? "," : "") + std::string("[") + JsonNumber(in.notes[i].seconds) + "," + std::to_string(in.notes[i].midi) + "]";
    j += "],\"mapping\":{";
    bool first = true;
    for (const auto& [name, key] : in.mapping) {
        j += (first ? "" : ",") + JsonString(name) + ":" + JsonString(key);
        first = false;
    }
    j += "},\"tempos\":[";
    for (size_t i = 0; i < in.tempos.size(); ++i)
        j += (i ? "," : "") + std::string("[") + JsonNumber(in.tempos[i].seconds) + "," + JsonNumber(in.tempos[i].bpm) + "]";
    j += "],\"meters\":[";
    for (size_t i = 0; i < in.meters.size(); ++i)
        j += (i ? "," : "") + std::string("[") + JsonNumber(in.meters[i].seconds) + "," + std::to_string(in.meters[i].numerator) + "]";
    j += "],\"regions\":[";
    for (size_t i = 0; i < in.regions.size(); ++i)
        j += (i ? "," : "") + std::string("[") + JsonNumber(in.regions[i].from) + "," + JsonNumber(in.regions[i].to) + "," + std::to_string(in.regions[i].semitones) + "]";
    j += "],\"options\":" + JsonOptions(in.options) + ",\"expected\":" + JsonString(expectedText) + "}";
    return j;
}

// The page's own style. The sheet keeps midi-converter's dark page and
// Verdana; the settings column is the shell's spacing on the same dark.
inline constexpr const char* kPageCss = R"css(
:root{--bg:#2D2A32;--side:#232027;--ink:#f2eff5;--muted:#aaa4b3;--line:rgba(255,255,255,.1);--field:#332f39;--accent:#7cc0ff}
*{box-sizing:border-box}
html,body{margin:0;height:100%}
body{background:var(--bg);color:var(--ink);font-family:Segoe UI,system-ui,sans-serif;font-size:13px;display:flex;flex-direction:column}
header{display:flex;align-items:center;gap:12px;padding:10px 16px;border-bottom:1px solid var(--line);background:var(--side)}
header h1{font-size:15px;font-weight:600;margin:0;flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
header .count{color:var(--muted);white-space:nowrap}
button{font:inherit;color:var(--ink);background:var(--field);border:1px solid var(--line);border-radius:6px;padding:6px 12px;cursor:pointer}
button:hover{border-color:rgba(255,255,255,.3)}
button.primary{background:var(--accent);color:#10131a;border-color:transparent;font-weight:600}
button.small{padding:2px 8px}
#layout{flex:1;display:flex;min-height:0}
aside{width:300px;flex:none;overflow:auto;padding:8px 16px 24px;border-right:1px solid var(--line);background:var(--side)}
main{flex:1;overflow:auto;padding:16px}
#sheet{white-space:pre-wrap;font-family:Verdana,sans-serif;font-size:10pt;line-height:135%;color:#fff}
#sheet .oor{display:inline-flex;justify-content:center;min-width:.6em;border-bottom:2px solid;font-weight:900}
#sheet .comment{color:#c8c4cc}
#sheet .chord.in-section{background:rgba(124,192,255,.16);border-radius:2px}
h2{font-size:11px;font-weight:600;color:var(--muted);text-transform:none;margin:20px 0 6px;letter-spacing:.02em}
label.row{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:30px}
label.col{display:block;margin:6px 0 10px}
label.col span{display:block;margin-bottom:4px}
p.note{color:var(--muted);margin:0 0 8px;font-size:12px}
input[type=text],input[type=number],select{font:inherit;color:var(--ink);background:var(--field);border:1px solid var(--line);border-radius:6px;padding:5px 8px;width:100%}
input[type=number]{width:90px}
input[type=range]{width:100%;accent-color:var(--accent)}
.range{display:flex;align-items:center;gap:8px}
.range output{min-width:64px;text-align:right;color:var(--muted)}
input[type=checkbox]{width:16px;height:16px;accent-color:#3fb950;margin:0}
#sections li{display:flex;align-items:center;gap:8px;list-style:none;padding:4px 0}
#sections{margin:0;padding:0}
#sections .empty{color:var(--muted)}
#selection{position:fixed;display:none;align-items:center;gap:8px;padding:8px 10px;background:var(--side);border:1px solid var(--line);border-radius:8px;box-shadow:0 8px 24px rgba(0,0,0,.4);z-index:2}
#selection output{min-width:2.5em;text-align:center}
#parity{display:none;margin:0 0 12px;padding:8px 12px;border:1px solid #c66;border-radius:6px;color:#f4b7b7}
#parity.shown{display:block}
@media print{header,aside,#selection,#parity{display:none!important}#layout{display:block}main{overflow:visible;padding:0}body{background:#2D2A32;-webkit-print-color-adjust:exact;print-color-adjust:exact}}
)css";

// sheet::Style, in JavaScript. Every function is named as in SheetExport.hpp
// and does the same arithmetic in the same order, which is what keeps a
// double-precision comparison between the two exact. No DOM in this half:
// tests/sheet-page-parity.js loads it on its own.
inline constexpr const char* kPageCore = R"js(/*SHEET-CORE-START*/
const SheetCore = (() => {
const CAPS = "!@#$%^&*()QWERTYUIOPASDFGHJKLZXCBVNM";
const LOWER = "1234567890qwertyuiopasdfghjklzxcvbnm";
const LOW_OOR = "1234567890qwert", HIGH_OOR = "yuiopasdfghj";
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const COLOURS = ["#9c0f00", "#ff1900", "#daa6a6", "#da7e5a", "#c0c05a", "#9ada5a", "#74da74", "#a3f0a3", "white"];
const CHORD = 0, BREAK = 1, COMMENT = 2, LONG = 8;
function defaults() {
  return {quantizeMs: 35, sequentialQuantize: true, curlyQuantizes: true, classicChordOrder: false, shifts: 0,
          outOfRangePlace: 2, showOutOfRange: true, outOfRangeMarks: false, outOfRangeSeparator: ":", tempoMarks: false,
          bpmChanges: true, bpmStyle: 0, minSpeedChange: 10, breaks: 0, beats: 4, missingBpm: 120, transpose: 0,
          autoTranspose: false, resilience: 2};
}
function noteName(midi) { return NAMES[midi % 12] + (Math.floor(midi / 12) - 1); }
function oneOf(character, set) { return character.length === 1 && set.indexOf(character) >= 0; }
function locate(ms, midi, mapping, o) {
  const p = {ms, beatMs: 500, midi, character: "", valid: midi >= 21 && midi <= 108, outOfRange: midi <= 35 || midi >= 97, display: midi};
  if (p.valid) {
    const name = noteName(midi);
    const found = Object.prototype.hasOwnProperty.call(mapping, name) ? mapping[name] : "";
    if (found) p.character = found.startsWith("ctrl+") ? found.slice(5) : found;
    else if (midi <= 35) p.character = LOW_OOR[midi - 21];
    else if (midi >= 97) p.character = HIGH_OOR[midi - 97];
    else p.valid = false;
  }
  if (oneOf(p.character, CAPS)) {
    if (o.shifts === 0) p.display = midi - 108;
    else if (o.shifts === 1) p.display = midi + 108;
  } else if (p.outOfRange) {
    if (o.outOfRangePlace === 0) p.display = midi - 1024;
    else if (o.outOfRangePlace === 1) p.display = midi + 1024;
    else p.display = oneOf(p.character, LOW_OOR) ? midi - 1024 : midi + 1024;
  }
  return p;
}
function classicOrder(notes) {
  notes = notes.slice().sort((a, b) => a.midi - b.midi);
  const start = [], end = [], numeric = [], upper = [], lower = [];
  let last = null;
  for (const n of notes) {
    if (n.outOfRange) {
      if (n.display === n.midi - 1024) start.push(n);
      else if (n.display === n.midi + 1024) end.push(n);
      continue;
    }
    if (n.character.length === 1 && n.character >= "0" && n.character <= "9") numeric.push(n);
    else if (oneOf(n.character, CAPS)) upper.push(n);
    else lower.push(n);
    last = n;
  }
  let out = start.slice();
  if (last) out = out.concat(oneOf(last.character, CAPS) ? numeric.concat(lower, upper) : upper.concat(numeric, lower));
  return out.concat(end);
}
function sortChord(notes, classic) {
  return classic ? classicOrder(notes) : notes.slice().sort((a, b) => a.display - b.display);
}
function item(kind) { return {kind, segments: [], text: "", separator: "", rhythm: LONG, ms: 0, beatMs: 500, msEnd: 0}; }
function renderChord(chord, quantized, o, r) {
  const it = item(CHORD);
  const nonOutOfRange = chord.filter(n => !n.outOfRange).length;
  let isChord = chord.length > 1 && chord.some(n => n.valid);
  if (!o.showOutOfRange && nonOutOfRange <= 1) isChord = false;
  const curly = quantized && o.curlyQuantizes;
  let firstStart = null, lastStart = null, firstEnd = null;
  for (const n of chord) {
    if (!n.outOfRange) continue;
    if (n.display === n.midi - 1024) { if (!firstStart) firstStart = n; lastStart = n; }
    else if (n.display === n.midi + 1024 && !firstEnd) firstEnd = n;
  }
  if (isChord) it.segments.push({text: curly ? "{" : "[", oor: false});
  for (const n of chord) {
    if (!n.valid) { it.segments.push({text: "_", oor: false}); r.unmapped++; continue; }
    const drawOor = n.outOfRange && o.showOutOfRange;
    if (drawOor) {
      const marked = n === firstStart || (!isChord && n === firstEnd) ||
        (isChord && nonOutOfRange === 0 && !firstStart && n === firstEnd) ||
        (isChord && nonOutOfRange > 0 && n === firstEnd);
      let text = n.character;
      if (o.outOfRangeMarks && marked) text = o.outOfRangeSeparator + text;
      it.segments.push({text, oor: true});
      r.notes++;
      if (o.outOfRangeMarks && n === lastStart && nonOutOfRange > 0) it.segments.push({text: "'", oor: false});
    } else if (!n.outOfRange) {
      it.segments.push({text: n.character, oor: false});
      r.notes++;
    } else {
      r.hidden++;
    }
  }
  if (isChord) it.segments.push({text: curly ? "}" : "]", oor: false});
  r.groups++;
  return it;
}
function transpositionScore(notes, by, mapping) {
  let good = 0, lower = 0, upper = 0;
  for (const note of notes) {
    const p = locate(0, note.midi + by, mapping, defaults());
    if (p.outOfRange || !p.valid) continue;
    good++;
    if (oneOf(p.character, LOWER)) lower++; else upper++;
  }
  return good * 2 + Math.abs(upper - lower);
}
function bestTransposition(notes, mapping, stickTo, resilience) {
  let best = transpositionScore(notes, stickTo, mapping);
  let bests = [stickTo];
  const consider = n => {
    const score = transpositionScore(notes, n, mapping);
    if (score > best + resilience) { best = score; bests = [n]; }
    else if (score === best) {
      if (n === 0 && bests.indexOf(0) >= 0) return;
      bests.push(n);
    }
  };
  for (let i = stickTo; i <= stickTo + 11; ++i) { consider(i); consider(-i); }
  return bests[0];
}
function bpmComment(previous, next, style, minimum) {
  const faster = next > previous;
  const larger = faster ? next : previous, smaller = faster ? previous : next;
  if (smaller <= 0) return null;
  const percent = Math.round((larger - smaller) / smaller * 100);
  if (percent < minimum) return null;
  const word = faster ? "faster" : "slower";
  if (style === 1) {
    const mark = faster ? ">" : "<";
    const increments = Math.max(1, Math.floor(percent / 10));
    if (increments > 20) return mark + " " + percent + "% " + word + " " + mark;
    return mark.repeat(increments);
  }
  return percent + "% " + word + " - BPM changed to " + next;
}
function rhythmFor(beat, d) {
  if (d < beat / 16) return 0;
  if (d < beat / 8) return 1;
  if (d < beat / 4) return 2;
  if (d < beat / 2) return 3;
  if (d < beat) return 4;
  if (d < beat * 2) return 5;
  if (d < beat * 4) return 6;
  if (d < beat * 8) return 7;
  return LONG;
}
function separator(beat, d) {
  if (d < beat / 4) return "-";
  if (d < beat / 2) return " ";
  if (d < beat) return " - ";
  if (d < beat * 2) return ", ";
  if (d < beat * 3) return "... ";
  if (d < beat * 4) return ".... ";
  return "...... ";
}
function tidyLines(text) {
  let out = "", started = false, blank = false;
  for (let line of text.split("\n")) {
    line = line.replace(/ +$/, "");
    if (!line) { blank = started; continue; }
    if (started) out += blank ? "\n\n" : "\n";
    out += line;
    started = true;
    blank = false;
  }
  return out;
}
)js"
R"js(
function applyRegions(notes, regions) {
  return notes.map(n => {
    let midi = n.midi;
    for (const region of regions) if (n.seconds >= region.from && n.seconds <= region.to) midi += region.semitones;
    return {seconds: n.seconds, midi};
  });
}
function style(notesIn, mapping, temposIn, metersIn, o) {
  const r = {items: [], text: "", notes: 0, groups: 0, merged: 0, unmapped: 0, hidden: 0, transposition: 0, hasTempo: false};
  const bySeconds = (a, b) => a.seconds - b.seconds;
  const notes = notesIn.slice().sort(bySeconds);
  const tempos = temposIn.slice().sort(bySeconds);
  const meters = metersIn.slice().sort(bySeconds);
  r.hasTempo = tempos.length > 0;
  r.transposition = o.autoTranspose && notes.length ? bestTransposition(notes, mapping, o.transpose, o.resilience) : o.transpose;
  const comment = text => { const it = item(COMMENT); it.text = text; r.items.push(it); };
  const lineBreak = () => r.items.push(item(BREAK));
  if (r.transposition !== 0) comment("Transpose by: " + (-r.transposition));
  const beatsAt = seconds => {
    let beats = 0, from = 0, bpm = o.missingBpm;
    for (const mark of tempos) {
      if (mark.seconds >= seconds) break;
      beats += (mark.seconds - from) * bpm / 60;
      from = mark.seconds;
      bpm = mark.bpm;
    }
    return beats + (seconds - from) * bpm / 60;
  };
  const events = [];
  tempos.forEach((t, i) => events.push({seconds: t.seconds, order: 0, index: i}));
  meters.forEach((m, i) => events.push({seconds: m.seconds, order: 1, index: i}));
  notes.forEach((n, i) => events.push({seconds: n.seconds, order: 2, index: i}));
  events.sort((a, b) => a.seconds !== b.seconds ? a.seconds - b.seconds : a.order - b.order);
  let bpm = o.missingBpm, previousBpm = 0, haveTempo = false, scheduled = false, numerator = 4;
  const START = 0, UNSET = 1, SET = 2;
  let next = START, nextBar = 0, penalty = 0, current = [], haveLast = false, last = 0;
  const flush = () => {
    if (!current.length) return;
    let quantized = false;
    for (let i = 1; i < current.length; ++i) if (current[i].ms !== current[i - 1].ms) { quantized = true; break; }
    let chord = [];
    if (!quantized) {
      for (const n of current) {
        if (chord.some(kept => kept.midi === n.midi)) { r.merged++; continue; }
        chord.push(n);
      }
      chord = sortChord(chord, o.classicChordOrder);
    } else if (o.sequentialQuantize) {
      chord = current.slice().sort((a, b) => a.ms - b.ms);
    } else {
      chord = sortChord(current, o.classicChordOrder);
    }
    const it = renderChord(chord, quantized, o, r);
    it.ms = chord[0].ms;
    it.beatMs = chord[0].beatMs;
    it.msEnd = chord.reduce((m, n) => Math.max(m, n.ms), chord[0].ms);
    r.items.push(it);
    current = [];
  };
  const checkBreak = first => {
    if (o.breaks === 0) {
      const beat = beatsAt(first.ms / 1000);
      if (scheduled) {
        scheduled = false;
        lineBreak();
        if (next !== START) next = UNSET;
      }
      if (next !== SET) { nextBar = (next === UNSET ? beat : 0) + numerator; next = SET; }
      if (beat + 1e-9 >= nextBar) { lineBreak(); nextBar += numerator; }
    } else if (o.breaks === 1) {
      const normalized = first.ms - penalty;
      if (normalized + 0.5 >= first.beatMs * o.beats) { lineBreak(); penalty += normalized; }
    }
  };
  for (const event of events) {
    if (event.order === 0) {
      const newBpm = tempos[event.index].bpm;
      scheduled = true;
      if (o.bpmChanges) {
        if (!haveTempo) comment("Tempo: " + Math.round(newBpm) + " BPM");
        else if (newBpm !== previousBpm) {
          const text = bpmComment(Math.round(previousBpm), Math.round(newBpm), o.bpmStyle, o.minSpeedChange);
          if (text !== null) comment(text);
        }
      }
      haveTempo = true;
      previousBpm = bpm = newBpm;
      continue;
    }
    if (event.order === 1) {
      numerator = Math.max(1, meters[event.index].numerator);
      scheduled = true;
      continue;
    }
    const note = notes[event.index];
    const placed = locate(note.seconds * 1000, note.midi + r.transposition, mapping, o);
    placed.beatMs = bpm > 0 ? 60000 / bpm : 500;
    if (!haveLast) { haveLast = true; last = placed.ms; }
    if (Math.abs(placed.ms - last) < o.quantizeMs) { current.push(placed); last = placed.ms; }
    else { flush(); current.push(placed); last = placed.ms; }
    checkBreak(current[0]);
  }
  flush();
  const kept = [];
  for (const it of r.items) {
    if (it.kind === BREAK && (!kept.length || kept[kept.length - 1].kind === BREAK)) continue;
    kept.push(it);
  }
  while (kept.length && kept[kept.length - 1].kind === BREAK) kept.pop();
  r.items = kept;
  const chords = [];
  r.items.forEach((it, i) => { if (it.kind === CHORD) chords.push(i); });
  for (let c = 0; c < chords.length; ++c) {
    const it = r.items[chords[c]];
    if (c + 1 === chords.length) { it.rhythm = LONG; it.separator = ""; continue; }
    const difference = r.items[chords[c + 1]].ms - it.ms - 0.5;
    it.rhythm = rhythmFor(it.beatMs, difference - 0.5);
    it.separator = o.tempoMarks ? separator(it.beatMs, difference) : " ";
  }
  let text = "";
  for (let i = 0; i < r.items.length; ++i) {
    const it = r.items[i];
    if (it.kind === CHORD) { for (const s of it.segments) text += s.text; text += it.separator; }
    else if (it.kind === COMMENT) text += "\n" + it.text + "\n";
    else {
      const nearComment = (i > 0 && r.items[i - 1].kind === COMMENT) || (i + 1 < r.items.length && r.items[i + 1].kind === COMMENT);
      if (!nearComment) text += "\n";
    }
  }
  r.text = tidyLines(text);
  return r;
}
function escapeHtml(text) {
  return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}
// The sheet as HTML, as sheet::ToHtml writes it, with each chord numbered so
// a selection can be traced back to its notes.
function toHtml(r) {
  let html = "";
  for (let i = 0; i < r.items.length; ++i) {
    const it = r.items[i];
    if (it.kind === CHORD) {
      html += '<span class="chord" data-i="' + i + '" style="color:' + COLOURS[it.rhythm] + '">';
      for (const s of it.segments) html += s.oor ? '<span class="oor">' + escapeHtml(s.text) + "</span>" : escapeHtml(s.text);
      html += escapeHtml(it.separator) + "</span>";
    } else if (it.kind === COMMENT) {
      html += '<br><span class="comment">' + escapeHtml(it.text) + "</span><br>";
    } else {
      const nearComment = (i > 0 && r.items[i - 1].kind === COMMENT) || (i + 1 < r.items.length && r.items[i + 1].kind === COMMENT);
      if (!nearComment) html += "<br>";
    }
  }
  return html;
}
return {defaults, applyRegions, style, toHtml, escapeHtml};
})();
/*SHEET-CORE-END*/
)js";

// The page itself: the settings column, the selection toolbar, copy, save
// and print. Reads the data element, draws on every change.
inline constexpr const char* kPageUi = R"js(
(() => {
const data = JSON.parse(document.getElementById("sheet-data").textContent);
const options = Object.assign(SheetCore.defaults(), data.options);
let regions = data.regions.map(r => ({from: r[0], to: r[1], semitones: r[2]}));
const notes = data.notes.map(n => ({seconds: n[0], midi: n[1]}));
const tempos = data.tempos.map(t => ({seconds: t[0], bpm: t[1]}));
const meters = data.meters.map(m => ({seconds: m[0], numerator: m[1]}));
const page = {fontSize: 10, lineHeight: 135};
const $ = id => document.getElementById(id);
let result = null;
const time = seconds => {
  const whole = Math.floor(seconds), minutes = Math.floor(whole / 60), rest = whole % 60;
  return minutes + ":" + (rest < 10 ? "0" : "") + rest + "." + Math.floor((seconds - whole) * 10);
};
function draw() {
  result = SheetCore.style(SheetCore.applyRegions(notes, regions), data.mapping, tempos, meters, options);
  const sheet = $("sheet");
  sheet.innerHTML = SheetCore.toHtml(result);
  sheet.style.fontSize = page.fontSize + "pt";
  sheet.style.lineHeight = page.lineHeight + "%";
  for (const span of sheet.querySelectorAll(".chord")) {
    const it = result.items[+span.dataset.i];
    const inside = regions.some(r => it.ms / 1000 >= r.from && it.msEnd / 1000 <= r.to);
    span.classList.toggle("in-section", inside);
  }
  let count = result.notes + " notes, " + result.groups + " chords.";
  if (result.merged) count += " " + result.merged + " shared notes merged.";
  if (result.unmapped) count += " " + result.unmapped + " unmapped.";
  if (result.hidden) count += " " + result.hidden + " out of range left out.";
  if (result.transposition) count += " Transposed " + (result.transposition > 0 ? "+" : "") + result.transposition + ".";
  $("count").textContent = count;
  const list = $("sections");
  list.innerHTML = "";
  if (!regions.length) list.innerHTML = '<li class="empty">Select part of the sheet to transpose it on its own.</li>';
  regions.forEach((r, i) => {
    const li = document.createElement("li");
    const text = document.createElement("span");
    text.textContent = time(r.from) + " to " + time(r.to) + ", " + (r.semitones > 0 ? "+" : "") + r.semitones + " semitones";
    text.style.flex = "1";
    const remove = document.createElement("button");
    remove.className = "small";
    remove.textContent = "Remove";
    remove.onclick = () => { regions.splice(i, 1); draw(); };
    li.append(text, remove);
    list.appendChild(li);
  });
  $("beats-row").style.display = options.breaks === 1 ? "" : "none";
}
function control(el) {
  const key = el.dataset.key, target = el.dataset.page ? page : options;
  const read = () => {
    if (el.type === "checkbox") return el.checked;
    if (el.type === "text") return el.value;
    return el.type === "range" || el.type === "number" || el.tagName === "SELECT" ? Number(el.value) : el.value;
  };
  const show = () => {
    if (el.type === "checkbox") el.checked = !!target[key];
    else el.value = target[key];
    const out = el.parentElement.querySelector("output");
    if (out) out.textContent = el.dataset.format ? el.dataset.format.replace("%", target[key]) : target[key];
  };
  show();
  el.addEventListener("input", () => { target[key] = read(); show(); draw(); });
}
document.querySelectorAll("[data-key]").forEach(control);
// A selection over the sheet names a section: from the first chord's onset to
// the last chord's last note, so every note in the chords selected moves.
const toolbar = $("selection");
let pending = null;
function selectionRegion() {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0 || selection.isCollapsed) return null;
  const range = selection.getRangeAt(0);
  let from = Infinity, to = -Infinity;
  for (const span of $("sheet").querySelectorAll(".chord")) {
    if (!range.intersectsNode(span)) continue;
    const it = result.items[+span.dataset.i];
    from = Math.min(from, it.ms / 1000);
    to = Math.max(to, it.msEnd / 1000);
  }
  return from <= to ? {from, to, semitones: 0} : null;
}
document.addEventListener("selectionchange", () => {
  const region = selectionRegion();
  if (!region) { if (!toolbar.contains(document.activeElement)) toolbar.style.display = "none"; return; }
  pending = region;
  const rect = window.getSelection().getRangeAt(0).getBoundingClientRect();
  toolbar.style.display = "flex";
  toolbar.style.left = Math.max(8, Math.min(window.innerWidth - toolbar.offsetWidth - 8, rect.left)) + "px";
  toolbar.style.top = Math.max(8, rect.top - toolbar.offsetHeight - 8) + "px";
  $("selection-range").textContent = time(region.from) + " to " + time(region.to);
  $("selection-amount").textContent = "0";
});
function transposeSelection(by) {
  if (!pending) return;
  const existing = regions.find(r => r.from === pending.from && r.to === pending.to);
  if (existing) existing.semitones += by; else regions.push({from: pending.from, to: pending.to, semitones: by});
  regions = regions.filter(r => r.semitones !== 0);
  const shown = regions.find(r => r.from === pending.from && r.to === pending.to);
  $("selection-amount").textContent = shown ? (shown.semitones > 0 ? "+" : "") + shown.semitones : "0";
  draw();
}
$("selection-down").onclick = () => transposeSelection(-1);
$("selection-up").onclick = () => transposeSelection(1);
$("selection-close").onclick = () => { toolbar.style.display = "none"; window.getSelection().removeAllRanges(); };
function copyText(text) {
  const fallback = () => {
    const area = document.createElement("textarea");
    area.value = text;
    document.body.appendChild(area);
    area.select();
    let ok = false;
    try { ok = document.execCommand("copy"); } catch (e) { ok = false; }
    area.remove();
    return ok;
  };
  if (navigator.clipboard && navigator.clipboard.writeText) return navigator.clipboard.writeText(text).then(() => true, fallback);
  return Promise.resolve(fallback());
}
$("copy").onclick = () => copyText(result.text).then(ok => {
  const button = $("copy");
  button.textContent = ok ? "Copied" : "Clipboard is busy";
  setTimeout(() => { button.textContent = "Copy sheet"; }, 1500);
});
$("save").onclick = () => {
  const saved = Object.assign({}, data, {options, regions: regions.map(r => [r.from, r.to, r.semitones]), expected: result.text});
  const element = $("sheet-data");
  const before = element.textContent;
  element.textContent = JSON.stringify(saved).replace(/<\//g, "<\\/");
  toolbar.style.display = "none";
  const html = "<!doctype html>\n" + document.documentElement.outerHTML;
  element.textContent = before;
  const link = document.createElement("a");
  link.href = URL.createObjectURL(new Blob([html], {type: "text/html"}));
  link.download = (data.title || "Sheet") + ".html";
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
};
$("print").onclick = () => window.print();
draw();
// The app wrote its own text of this sheet into the page. A difference means
// the two translations of midi-converter have drifted, which is a bug here.
if (typeof data.expected === "string" && data.expected !== result.text) {
  console.error("The page's sheet differs from the app's. Expected:\n" + data.expected + "\nGot:\n" + result.text);
  $("parity").classList.add("shown");
}
})();
)js";

} // namespace detail

// The editable page. The initial sheet is the app's own rendering, which the
// script replaces with its own on load and checks against.
inline std::string ToEditorHtml(const PageInput& in) {
    std::vector<TimedNote> notes = in.notes;
    for (auto& note : notes)
        for (const auto& region : in.regions)
            if (note.seconds >= region.from && note.seconds <= region.to) note.midi += region.semitones;
    const auto initial = Style(std::move(notes), in.mapping, in.tempos, in.meters, in.options);
    std::string html = "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><title>" + detail::EscapeHtml(in.title) +
        "</title><style>" + detail::kPageCss + "</style></head><body>\n"
        "<header><h1>" + detail::EscapeHtml(in.title) + "</h1><span class=\"count\" id=\"count\"></span>"
        "<button id=\"copy\" class=\"primary\">Copy sheet</button><button id=\"save\">Save page</button><button id=\"print\">Print</button></header>\n"
        "<div id=\"layout\"><aside>\n"
        "<h2>Chords</h2>"
        "<label class=\"col\"><span>Chord window</span><div class=\"range\"><input type=\"range\" min=\"0\" max=\"200\" step=\"1\" data-key=\"quantizeMs\" data-format=\"% ms\"><output></output></div></label>"
        "<p class=\"note\">A note this close to the one before joins its chord.</p>"
        "<label class=\"row\"><span>Keep spread chords in played order</span><input type=\"checkbox\" data-key=\"sequentialQuantize\"></label>"
        "<label class=\"row\"><span>Write spread chords in braces</span><input type=\"checkbox\" data-key=\"curlyQuantizes\"></label>"
        "<label class=\"row\"><span>Classic chord order</span><input type=\"checkbox\" data-key=\"classicChordOrder\"></label>"
        "<label class=\"col\"><span>Shifted characters</span><select data-key=\"shifts\"><option value=\"0\">First in the chord</option><option value=\"1\">Last in the chord</option><option value=\"2\">In pitch order</option></select></label>\n"
        "<h2>Out of range</h2>"
        "<label class=\"row\"><span>Keep out-of-range notes</span><input type=\"checkbox\" data-key=\"showOutOfRange\"></label>"
        "<label class=\"row\"><span>Mark out-of-range notes</span><input type=\"checkbox\" data-key=\"outOfRangeMarks\"></label>"
        "<label class=\"col\"><span>Separator</span><input type=\"text\" maxlength=\"7\" data-key=\"outOfRangeSeparator\"></label>"
        "<label class=\"col\"><span>Out-of-range notes sit</span><select data-key=\"outOfRangePlace\"><option value=\"0\">First in the chord</option><option value=\"1\">Last in the chord</option><option value=\"2\">Low first, high last</option></select></label>\n"
        "<h2>Rhythm and tempo</h2>"
        "<label class=\"row\"><span>Rhythm separators</span><input type=\"checkbox\" data-key=\"tempoMarks\"></label>"
        "<p class=\"note\">The gap after a chord becomes a dash, a comma or a bar by its length.</p>"
        "<label class=\"row\"><span>Mention tempo changes</span><input type=\"checkbox\" data-key=\"bpmChanges\"></label>"
        "<label class=\"col\"><span>Tempo change wording</span><select data-key=\"bpmStyle\"><option value=\"0\">Detailed</option><option value=\"1\">Arrows</option></select></label>"
        "<label class=\"col\"><span>Smallest tempo change mentioned</span><div class=\"range\"><input type=\"range\" min=\"0\" max=\"100\" step=\"1\" data-key=\"minSpeedChange\" data-format=\"%%\"><output></output></div></label>"
        "<label class=\"col\"><span>Line breaks</span><select data-key=\"breaks\"><option value=\"0\">Every bar</option><option value=\"1\">Every few beats</option><option value=\"2\">None</option></select></label>"
        "<label class=\"col\" id=\"beats-row\"><span>Beats per line</span><div class=\"range\"><input type=\"range\" min=\"1\" max=\"32\" step=\"1\" data-key=\"beats\" data-format=\"% beats\"><output></output></div></label>"
        "<label class=\"col\"><span>Tempo when the file names none</span><div class=\"range\"><input type=\"range\" min=\"20\" max=\"400\" step=\"1\" data-key=\"missingBpm\" data-format=\"% BPM\"><output></output></div></label>\n"
        "<h2>Transposition</h2>"
        "<label class=\"col\"><span>Transpose</span><div class=\"range\"><input type=\"range\" min=\"-24\" max=\"24\" step=\"1\" data-key=\"transpose\" data-format=\"% semitones\"><output></output></div></label>"
        "<label class=\"row\"><span>Find the best transposition</span><input type=\"checkbox\" data-key=\"autoTranspose\"></label>"
        "<p class=\"note\">Searches near Transpose for the shift that keeps the most notes on keys.</p>"
        "<label class=\"col\"><span>Resilience</span><div class=\"range\"><input type=\"range\" min=\"0\" max=\"20\" step=\"1\" data-key=\"resilience\"><output></output></div></label>"
        "<p class=\"note\">How much better a shift must score to replace Transpose.</p>\n"
        "<h2>Sections</h2><ul id=\"sections\"></ul>\n"
        "<h2>Page</h2>"
        "<label class=\"col\"><span>Text size</span><div class=\"range\"><input type=\"range\" min=\"6\" max=\"24\" step=\"1\" data-key=\"fontSize\" data-page=\"1\" data-format=\"% pt\"><output></output></div></label>"
        "<label class=\"col\"><span>Line height</span><div class=\"range\"><input type=\"range\" min=\"100\" max=\"250\" step=\"5\" data-key=\"lineHeight\" data-page=\"1\" data-format=\"%%\"><output></output></div></label>"
        "<p class=\"note\">Print, or a screenshot, is the image of this sheet.</p>"
        "</aside>\n<main><p id=\"parity\">The page's sheet differs from the app's. Copy styled sheet in the app gives the app's version.</p>"
        "<div id=\"sheet\">";
    // The app's own rendering, so the sheet is there before the script runs.
    html += detail::SheetBody(initial);
    html += "</div></main></div>\n"
        "<div id=\"selection\"><span id=\"selection-range\"></span><span>Transpose</span>"
        "<button id=\"selection-down\" class=\"small\">-</button><output id=\"selection-amount\">0</output>"
        "<button id=\"selection-up\" class=\"small\">+</button><button id=\"selection-close\" class=\"small\">Done</button></div>\n"
        "<script id=\"sheet-data\" type=\"application/json\">" + detail::PageJson(in, initial.text) + "</script>\n"
        "<script>" + detail::kPageCore + detail::kPageUi + "</script>\n</body></html>\n";
    return html;
}

} // namespace sheet
