// Runs the coloured page's own script over the fixtures ShellTests writes
// and requires the page to produce the app's text, style for style. The
// page draws with SheetPage.hpp's JavaScript translation of sheet::Style;
// this is what keeps that translation honest.
//
//   node tests/sheet-page-parity.js build/shell-tests/sheet-page.html build/shell-tests/sheet-page-parity.json
"use strict";
const fs = require("fs");

const [htmlPath, fixturePath] = process.argv.slice(2);
if (!htmlPath || !fixturePath) {
    console.error("usage: node sheet-page-parity.js <sheet-page.html> <sheet-page-parity.json>");
    process.exit(2);
}
const html = fs.readFileSync(htmlPath, "utf8");
const start = html.indexOf("/*SHEET-CORE-START*/"), end = html.indexOf("/*SHEET-CORE-END*/");
if (start < 0 || end < 0) { console.error("FAIL the page carries no sheet script"); process.exit(1); }
const SheetCore = new Function(html.slice(start, end) + "\nreturn SheetCore;")();

const unpack = data => ({
    notes: data.notes.map(n => ({seconds: n[0], midi: n[1]})),
    tempos: data.tempos.map(t => ({seconds: t[0], bpm: t[1]})),
    meters: data.meters.map(m => ({seconds: m[0], numerator: m[1]})),
    regions: data.regions.map(r => ({from: r[0], to: r[1], semitones: r[2]})),
    mapping: data.mapping,
    options: Object.assign(SheetCore.defaults(), data.options),
});
const render = (d, options) =>
    SheetCore.style(SheetCore.applyRegions(d.notes, d.regions), d.mapping, d.tempos, d.meters, options).text;
const show = text => JSON.stringify(text.length > 400 ? text.slice(0, 400) + "..." : text);

let failures = 0, checks = 0;
const check = (name, expected, got) => {
    checks++;
    if (expected === got) return;
    failures++;
    console.error("FAIL " + name + "\n  app:  " + show(expected) + "\n  page: " + show(got));
};

// The page's own data against the text the app wrote into it.
const dataStart = html.indexOf('<script id="sheet-data" type="application/json">');
const dataEnd = html.indexOf("</script>", dataStart);
if (dataStart < 0 || dataEnd < 0) { console.error("FAIL the page carries no data"); process.exit(1); }
const page = JSON.parse(html.slice(dataStart + '<script id="sheet-data" type="application/json">'.length, dataEnd));
const pageData = unpack(page);
check("the page's own sheet", page.expected, render(pageData, pageData.options));

// The fixture: one score, several styles.
const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
const data = unpack(fixture.data);
for (const c of fixture.cases) check(c.name, c.expected, render(data, Object.assign(SheetCore.defaults(), c.options)));

if (failures) { console.error(failures + " of " + checks + " sheet page checks failed"); process.exit(1); }
console.log("PASS sheet page parity: " + checks + " sheets drawn by the page match the app");
