# Who does what, and why

Written 2026-09-07. Two assistant seats work this repo: Claude Opus 5, and a
second seat currently filled by GPT-6 Astra in the Codex app. This page divides
the work by what each is measurably better at, replacing a split that was drawn
by directory alone.

## What the published numbers actually say

Checked 2026-09-07 against Artificial Analysis, llm-stats and several
comparison writeups. Read the whole picture before using any single figure:
**on ordinary work the two are hard to tell apart.** Artificial Analysis has
Opus 5 at 63 and Astra at 61 overall, the coding lane at 75.6 to 75.3, and the
Intelligence Index result flips depending on which effort levels are paired.
Two points is not a reason to route anything.

The separations that are real and large:

| | Astra | Opus 5 |
|---|---|---|
| Browser and computer use | **77.3%** | 50.5% |
| OSWorld 2.0, Terminal-Bench 4.0, DeepSWE 1.1, AutomationBench, ARC-AGI-3, BrowseComp | **wins** | |
| FrontierCode 1.1 | | **wins** |
| Public agentic tasks lane | 70.4 | **77.4** |
| Tokens spent per coding-agent task | **about one fifth** | baseline |
| Price per token | 2x | **1x** |

Two consequences worth stating plainly:

- The computer-use gap is the only one that is not close. Anything that has to
  drive the machine goes to Astra on the strength of a 27 point margin, not a
  two point one.
- Astra costs twice as much per token and spends about a fifth as many on
  agent tasks, so on that band it is **cheaper in practice**, not more
  expensive. That matters here, because the owner works against subscription
  limits rather than a per-token bill.

## The division

### Astra

- **Anything that drives the machine.** Launching the app, clicking through
  the shell, verifying behaviour in the game, DPI-correct screenshots, render
  passes across skins. This is the 77.3 against 50.5, and it is also where
  Opus 5 has already wasted effort on this repo: the display is at 125% and a
  naive click script is off by a quarter.
- **The panel port and the parity build-out.** `ui/`, many files, long
  build-run-look loops. Long-horizon terminal work is Terminal-Bench and
  OSWorld, both Astra's.
- **Every fix that has something to iterate against.** A failing test, a
  repro, a stack trace, a build error, or a symptom narrowed to one file.
  DeepSWE 1.1 and Terminal-Bench 4.0 are precisely this task, both Astra's, and
  it is the owner's own read of the seat. When there is something to run, this
  seat both finds and fixes it: do not split a task that one seat can close.
- **In-game and interactive verification**, which was already this seat's under
  the earlier agreement.

### Opus 5

- **Specifications, seams and inventories.** `MIDI-OUTPUT.md`, `SHELL-GAPS.md`
  and this page. Work whose output is read by the other seat before it writes
  code.
- **Reports with nothing to run.** Not "fix this bug", which is Astra's, but
  "three people described something odd and nobody knows which subsystem". The
  ALT velocity tap behind three separate tester reports was found by reading
  `PlaybackCore.cpp` against `config.json`, because it cannot be reproduced on
  this machine at all: it only manifests inside a game tab on someone else's
  computer. An agent loop needs something to iterate against and there was
  nothing. That is the distinction, and it is the only one. DeepSWE hands the
  model the issue **and** the repo's test; a Discord thread hands it neither.
  Once this seat has named the file and the line, the fix goes to Astra.
- **Tests.** `tests/` is already this seat's, tests are read-heavy and
  run-light, and they cost half as much here.
- **Holding the repo's history in view.** Which decisions were made, what
  `HANDOFF.md` already answers, and which reported bug is a known open item
  rather than a new one.

### Shared rule, unchanged

Whoever owns the panel owns `ShellEngine::Action` and `EngineSnapshot`. The
other seat consumes them and never extends them. Ask for an action that does
not exist and wait. This rule exists because on 2026-09-05 both seats built the
same transport engine from the same prompt and one had to be thrown away.

## What changed from the old split

The old split was `ui/` against `MIDI++/` and `tests/`. The evidence mostly
agrees with it, so most of this is a confirmation rather than a rewrite. Three
real changes:

1. Verification moves to Astra completely, including the measured and
   DPI-sensitive work, not just the checks that take the cursor.
2. Fixing moved to Astra, not just verifying it. The first draft of this page
   kept bounded engine fixes on Opus 5 and gave Astra only the fixes that
   needed the app running, which was this seat marking its own homework: the
   benchmarks that measure resolve-an-issue-end-to-end are DeepSWE and
   Terminal-Bench, and Astra takes both. Diagnosis splits from fixing only when
   there is nothing to run, and it rejoins the moment a file and line are
   named.
3. The division has a stated basis, so it can be revised when the next numbers
   land instead of being inherited.

## The test for which seat

One question, in this order, and it settles almost everything.

1. Does it need to drive the machine, or look at pixels? Astra.
2. Is there something to iterate against, a repro, a failing test, a build
   error, or a named file and line? Astra, finding and fixing both.
3. Is the output a document another seat reads before writing code? Opus 5.
4. Is it a report nobody can reproduce yet? Opus 5, until it becomes case 2,
   which it then is.

A seat that finds itself arguing for its own column against this test should
lose the argument.

Sources: [Artificial Analysis
comparison](https://artificialanalysis.ai/models/comparisons/gpt-6-astra-medium-vs-claude-opus-5),
[Benchmarking GPT-6
Astra](https://artificialanalysis.ai/articles/benchmarking-gpt-6-astra),
[llm-stats](https://llm-stats.com/models/compare/claude-opus-5-vs-gpt-6-astra),
[browser-use
benchmark](https://www.explainx.ai/blog/gpt-6-astra-browser-use-benchmark-v2-claude-opus-5-2026).
