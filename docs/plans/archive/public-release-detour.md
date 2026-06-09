# Plan — public-release detour

Status: **DONE — all five tasks landed 2026-05-29.** Authored at the end
of the HOUSE hikari session; executed the same day. Five loosely-coupled
tasks that prepare the repo to go public. Research findings that de-risk
each task are embedded inline. Task spec preserved below as the record.

Landing summary (commits, in order):
- **Task 5** `6dd26a7` — `docs/reference/vendor-exe.md` (App ID 70400
  confirmed; per-section encryption map; reproducible hashes).
- **Task 2** `f4b597d` — runtime SE extraction. New `src/se_pack.c` +
  `src/sha256.c`; cache at `%LOCALAPPDATA%\openrecet\se.pack` keyed on the
  retail-exe sha256; `audio.c` sources SE from the cache; SE_RC/SE_RES_O
  dropped from `src/Makefile`. Verified: built exe has 0 RIFF; first run
  extracts 109/110 from the retail exe, second run loads the cache;
  2902 host tests (+8). `docs/formats/se-pack.md`.
- **Task 3** `6643ba9` — `.github/workflows/nightly.yml` (daily cron +
  manual dispatch) builds via a new lean `devShells.ci`, gated by
  `tools/ci/no_proprietary_bytes.py`, publishes to a rolling `nightly`
  pre-release. Decisions: cache at LOCALAPPDATA, daily cron.
- **Tasks 1+4** `726254e` — public README + ko-fi/AI-transparency; hero is
  a labeled OpenRecet-vs-retail HOUSE side-by-side
  (`docs/img/house-comparison.png`). Status framed as early/not-playable,
  detail deferred to STATUS.md / port-ledger.md per user direction.

Decisions made during execution (were open questions):
- SE cache dir → `%LOCALAPPDATA%\openrecet\` (user choice).
- Nightly cadence → daily cron + manual dispatch (user choice).
- `se.pack` format → see `docs/formats/se-pack.md` (decided in-session).
- README screenshot → labeled side-by-side hero with 2× clean labels;
  HUD difference handled by a broad "early-stage / not playable" framing
  + pointer to the ledger, not an itemized broken-features list.

Not yet done (out of this session's hands): the nightly workflow only
runs once the repo is public on GitHub with Actions enabled, and the
`nightly` pre-release is auto-created on first run (no manual step needed).

## Why a detour

We now have considerable *visible* output (title → new-game → HOUSE shop
rendering near-retail 1:1). Good moment to make the repo public, show it
off, and set up nightly builds — but only after we guarantee the
distributed binary contains **zero proprietary bytes**.

## Hard dependency order

```
Task 2 (stop embedding SE)  ─┐
                             ├─►  Task 3 (CI nightly release)
Task 5 (vendor-exe doc)     ─┘     [gated: "no proprietary bits" proof]
Task 1 + 4 (README + ko-fi) ── independent, do any time
```

Task 3 **must not** run until Task 2 lands — today's build links the
proprietary SE WAVs into the exe (`se.res.o`), so a public binary would
redistribute copyrighted audio. Task 2 removes that; Task 3's pipeline
should hard-fail if any embedded-asset signature reappears.

---

## Task 2 — extract SE from the vendor exe at runtime (don't embed)

**Goal:** the shipped `openrecet.exe` carries no game audio. On first run
it reads the 110 SE WAVs out of the user's own retail `recettear.exe`,
decodes/normalises them, and caches them under our own port-data folder.
Subsequent runs load from the cache.

**KEY RESEARCH FINDING (verified this session — removes the hard part):**
SteamStub on this exe encrypts **only `.text`**. Per-section sha256 of
packed vs Steamless-unpacked:

| section | packed vs unpacked |
|---|---|
| `.text`  | **DIFFERS** (encrypted) |
| `.rdata` | identical |
| `.data`  | identical |
| `.data1` | identical |
| `.rsrc`  | identical |
| `.extra` | identical |

The SE live in `.rsrc` (custom PE resource type **`WAVE`**), and the
110-entry SE id table is at VA `0x005d1584` in `.data` (8-byte stride:
`u32 resource_id` + `u32 channel_flag`; the +4 column is all-zero in
shipped data — see `engine-quirks.md` #46). **Both are byte-identical in
the packed retail exe.** So the port does **NOT** need to replicate
Steamless / decrypt anything — it reads the *packed* `recettear.exe`
directly. The framing "do enough of what steamless does" turned out
unnecessary; resources aren't protected.

**Reference implementation already exists in Python:**
`tools/extract/se-wavs.py` parses the `.rsrc` tree under type `WAVE`,
reads the id table, and dumps blobs to `vendor/unpacked/se-extracted/`.
Port that logic to C (or keep a build/first-run step) — it's plain PE
resource walking, no crypto.

**Two viable extraction mechanisms (pick one in the executing session):**

1. **Windows resource API (simplest, recommended).**
   `LoadLibraryExA(retail_exe, NULL, LOAD_LIBRARY_AS_DATAFILE)` then
   `FindResourceA(h, MAKEINTRESOURCE(id), "WAVE")` / `LoadResource` /
   `LockResource` per id from the `0x5d1584` table. The OS maps the file
   as data (no code execution, SteamStub never runs), and `.rsrc` is
   unencrypted, so this Just Works. ~40 lines of C. No PE parser needed.
2. **Hand-rolled PE `.rsrc` walker in C** (port of `se-wavs.py`). More
   code, but zero dependency on the resource being a *loadable* image
   and works cross-platform for tooling. Overkill given (1) works.

**Where to find the retail exe at runtime:** reuse the existing vendor
path discovery. `vendor/original` is a symlink to the Steam install;
the port already knows the game dir (`tools/run-openrecet.sh` banner
prints it; `OPENRECET_*` / recet.ini). On first run, locate
`recettear.exe` next to the game data; if absent, print a clear message
("point OPENRECET_GAME_DIR at your Recettear install").

**Port-data cache folder (user explicitly wants this separate from
retail):** define one, e.g. `%LOCALAPPDATA%\openrecet\` on Windows or a
`./openrecet-data/` next to our exe. Store the extracted SE as a single
packed binary (the user said "maybe store them as a binary") — e.g. a
simple `se.pack` (header + offset table + concatenated WAV/PCM blobs),
versioned by the retail exe's sha256 so a different game version
re-extracts. Document the format under `docs/formats/se-pack.md`.

**Steps:**
1. Decide cache location + `se.pack` container format (write the spec).
2. Implement C extractor (mechanism 1) — produces the in-memory SE set
   from `recettear.exe`.
3. Implement `se.pack` writer (first run) + reader (subsequent runs),
   keyed on retail-exe sha256.
4. Rip out the build-time embedding: drop `SE_RC`/`SE_RES_O` from
   `src/Makefile` `OBJS`, delete the `se.rc`/`se.res.o` rules, retire
   `tools/extract/se-rc.py` (keep `se-wavs.py` as the reference / a
   dev fallback). Audio loader switches from the linked resource to the
   `se.pack` cache.
5. Verify: built exe no longer contains any WAV bytes (grep the binary
   for `RIFF`/`WAVE` magic → none); game audio still plays via the
   runtime-extracted cache.
6. **Audit for any OTHER embedded proprietary data** (this session
   checked: the ONLY embedded asset is `se.res.o`; fonts
   `fontdata.bin`/`fontidx.bin` are already loaded at runtime from
   storage, not embedded). Re-confirm after the change.

**Acceptance:** `openrecet.exe` built clean contains no game audio (no
`RIFF` magic, no WAVE resource); first run extracts to `se.pack`; audio
parity unchanged.

---

## Task 5 — document the exact reference exe

**Goal:** a permanent record identifying the precise binary all our RE is
against, so future version-diffing has an anchor. Create
`docs/reference/vendor-exe.md` with the data below (all gathered this
session — ready to drop in):

```
Game:            Recettear: An Item Shop's Tale (EasyGameStation, 2007;
                 Carpe Fulgur EN localisation)
Window title:    "RECETTEAR Ver 1.108"   (src/main.c:81 AZUMANGA_TITLE)
Steam App ID:    70400  (CONFIRM — well-known but verify against the
                 install before publishing)
Distribution:    Steam (EN build)

Packed (retail, as shipped on Steam):
  path           <Steam>/steamapps/common/Recettear/recettear.exe
  size           5,629,440 bytes
  sha256         079b5b679f1d363ea3dcfe4ec931ceb6d9e4b4a288926ffc8abb5814e80392b4
  PE timestamp   1286439830 = 2010-10-07 08:23:50 UTC
  machine        0x14c (i386, 32-bit)
  sections       .text .rdata .data .data1 .rsrc .extra .bind
  protection     SteamStub (the .bind section); encrypts .text ONLY

Unpacked (Steamless output — what we disassemble/hex):
  path           vendor/unpacked/recettear.unpacked.exe
  size           5,268,992 bytes
  sha256         ab7d1d952150f3a5a5f1acf0c230d438a6882e5f5be88bb79972d4100ca7c27d
  sections       .text .rdata .data .data1 .rsrc .extra  (.bind stripped)
  PE timestamp   1286439830 (unchanged)

Steamless:       v3.1.0.5 (by atom0s)
                 https://github.com/atom0s/Steamless/releases
                 run via WSLInterop; see tools/setup.sh + flake.nix
                 (OPENRECET_STEAMLESS_DIR)

SteamStub encryption map (per-section packed-vs-unpacked sha256, this
exe): ONLY .text differs. .rdata/.data/.data1/.rsrc/.extra are
byte-identical → resources + data are readable from the packed exe
without unpacking (basis for Task 2).
```

Also note: `vendor/unpacked/.unpacked.sha256` already records the
unpacked hash; this doc is the human-readable superset. Link it from
`README.md` and `docs/STATUS.md`.

**Acceptance:** doc committed; hashes reproducible via `sha256sum`.

---

## Task 1 — dense public README

**Goal:** replace the current `README.md` with a dense, show-off-ready
status page for a public audience.

**Sections to include:**
- **One-liner:** clean-room-ish C reimplementation of Recettear's Win32
  engine ("Azumanga"), a drop-in `recettear.exe` replacement. MIT.
- **Status / what works now** (lead with the visuals): title → new-game
  flow, HOUSE shop rendered in 3D near-retail-1:1 (brightness, blinds,
  curtains/hikari god-rays all matched via D3D-trace A/B). Pull the
  port-coverage headline from `docs/STATUS.md` (currently ~14% of
  non-thunk engine functions touched). Embed 1–2 side-by-side
  port-vs-retail screenshots (generate fresh; the HOUSE shot from this
  session is a good hero image — but render WITHOUT retail HUD overlay
  confusion, or label clearly).
- **How it's verified:** behavioural parity vs the original exe —
  per-draw D3D state traces, call-trace diffing, frame captures, 2894
  host tests under ASan/UBSan. (This is a strong credibility signal.)
- **Build/run:** `nix develop` + `make -C src`; needs your own legit
  copy of Recettear (we ship no assets). Point at `docs/STATUS.md` /
  `AGENT-WORKFLOW.md` for depth.
- **Legal:** not affiliated with EGS/Carpe Fulgur; ships no game assets;
  requires the user's own purchased copy; reads SE from the user's exe
  at runtime (Task 2). Reference the exact version (Task 5 doc).
- **AI-driven transparency** (see Task 4).
- **Support / ko-fi** (see Task 4).

**Acceptance:** README renders well on GitHub; screenshots load; links
valid.

---

## Task 4 — ko-fi + AI-transparency (folds into the README)

Exact copy the user wants (paraphrase tastefully, keep the facts):

- **Support:** ko-fi <https://ko-fi.com/lolisamurai>. Donations go
  toward paying for Claude. **~€240 ≈ one month of intense RE work**,
  spread across multiple projects (2–4 at a time to use the subscription
  efficiently). **Games suggested in donations will be considered as
  future RE targets.**
- **Transparency:** the RE and documentation work in this repo is
  **entirely AI-driven** (Claude), directed by the maintainer. State this
  plainly — it's a feature (reproducible methodology) and honest
  disclosure.

Place as its own short "Support & how this is made" section near the
bottom of the README, above Legal.

**Acceptance:** ko-fi link present + correct; AI-driven disclosure
unambiguous.

---

## Task 3 — CI nightly release (gated on Task 2)

**Goal:** GitHub Actions builds `openrecet.exe` on a schedule/commit and
publishes it as a rolling **nightly** — *without* spamming repo watchers
with a release notification on every build.

**Build in CI:** the repo builds via Nix + mingw cross-compiler. Use
`DeterminateSystems/nix-installer-action` (or `cachix/install-nix-action`)
then `nix develop --command make -C src`. No Windows runner needed
(mingw cross-compile produces the PE on Linux). Cache the Nix store
(`magic-nix-cache` / cachix) to keep builds fast.

**The "don't spam followers" requirement — how GitHub notifications
work:** users who "Watch → Releases" get an email/notification when a
**new release is published**. Updating the **assets of an existing
release does NOT notify**. So:
- Maintain **one** long-lived release with a fixed tag, e.g. `nightly`
  (or `continuous`). Each CI run **deletes+recreates that tag's assets**
  (or edits the release in place) rather than creating a new release.
  Watchers are notified at most once (when `nightly` first appears).
- Mark it **pre-release** (`prerelease: true`) — pre-releases are
  excluded from the repo's "Releases" headline and the Atom feed's
  "latest", further reducing noise.
- Tooling: `softprops/action-gh-release` with a fixed `tag_name: nightly`
  + `prerelease: true`, or the `gh release upload --clobber` CLI. Set the
  release body to the short git log since last nightly + the source
  commit sha, but keep the *release* object stable.
- **Do NOT** cut versioned `vX.Y` releases on every commit — those DO
  notify. Reserve real tags for intentional milestones.
- Schedule: `on: schedule` (cron, e.g. daily) + manual `workflow_dispatch`;
  avoid `on: push` per-commit unless deduped to the rolling release.

**Proprietary-bytes gate (CI must enforce):** before uploading, scan the
built exe for game-asset signatures (no `RIFF`/`WAVE` blobs; size sanity;
optionally a denylist of known SE byte-prefixes). Fail the job if any
hit. This is the automated guarantee behind "100% sure no proprietary
bits". Document the check so it's auditable.

**Steps:**
1. (After Task 2) add `.github/workflows/nightly.yml`: Nix build →
   proprietary-bytes scan → `gh release upload --clobber nightly`.
2. Create the `nightly` pre-release once, manually, with clear "automated
   nightly, unsigned, bring your own Recettear" notes.
3. Verify a second run updates assets WITHOUT a new notification (check
   from a watching test account or confirm via the API that no new
   release object is created).

**Acceptance:** two consecutive CI runs publish updated `openrecet.exe`
to the single `nightly` pre-release; no per-build release notification;
proprietary-bytes scan is green and gates the upload.

---

## Open questions for the user (resolve at execution time)
- Cache/port-data folder location + `se.pack` format details (Task 2).
- Confirm Steam App ID 70400 (Task 5).
- Nightly cadence: daily cron vs per-push-deduped (Task 3).
- Which screenshots to feature in the README (Task 1) — and whether to
  strip the retail HUD from comparison shots to avoid confusion.
