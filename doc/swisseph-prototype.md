# Local Swiss Ephemeris asteroid prototype

This opt-in experiment supplies geometric positions and velocities for **numbered
asteroids with installed Swiss Ephemeris files**. Ceres, Pallas, Juno and Vesta use
the native main-asteroid chunks; other numbered asteroids use individual files.
Users choose their own catalogue and downloaded files.
There is no artificial 1000-object cutoff. Pluto (134340) retains Stellarium's
existing planetary backend. This is not a release-ready licensing or accuracy
endorsement. No automatic downloads happen during application startup.

## Build from a fresh clone

Clone this branch, enter the source root, and install the normal Stellarium build
requirements described in [BUILDING.md](../BUILDING.md). Also install Python 3
(the fetcher uses only its standard library) and Ninja for the example below.
Run these commands from the source root in a POSIX shell (Linux/macOS):

```sh
python3 util/fetch_swisseph.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SWISSEPH=ON -DSWISSEPH_SOURCE_DIR="$PWD/build/swisseph-deps/source"
cmake --build build --target stellarium --parallel 6
```

The fetcher downloads the pinned Swiss C source and license notices into
`build/swisseph-deps/source`. It downloads **no ephemeris files**. Internet access
to GitHub is needed for this step, but neither a GitHub account nor Python packages
are required. CMake does not fetch Swiss automatically. It compiles the downloaded
source into Stellarium; no separately installed Swiss library is needed at runtime.
Keep any additional platform-specific CMake options required by BUILDING.md.

For Windows, use the toolchain and shell described in BUILDING.md, run the fetcher
with your Python 3 command (for example `py -3 util/fetch_swisseph.py`), and set
`SWISSEPH_SOURCE_DIR` to the absolute source directory, for example
`C:/work/stellarium/build/swisseph-deps/source`. A multi-configuration generator
also needs `--config Release` when building. This integration's build/run validation
has so far been on Linux; Windows and macOS remain unverified.

Launch the resulting Stellarium executable and download the desired ephemerides
through the GUI described below. Import the desired asteroid catalogue separately.
Without ephemerides, asteroid positions retain the existing Kepler fallback.
Tests and test-data downloads are optional developer steps, not prerequisites for
using the application; see the reproducible-test section below.

On Linux, the optional `./util/run_swisseph.sh` helper launches
`build/src/stellarium` with a persistent separate profile at `~/.stellarium-swisseph`.
Set `SWISSEPH_USER_DIR` to choose another profile. The helper clears inherited
`SWEPH_PATH` and `STEL_USERDIR` overrides so calculations and GUI downloads use
the selected profile together.
It does not copy or download ephemerides. Use the GUI in that profile to select data.
Normal settings are not modified by this helper.
An enabled build uses `<active user directory>/ephemeris/swiss` automatically;
on Linux the normal profile is `~/.stellarium`, while `--user-dir` and
`STEL_USERDIR` select other profiles. No installed application files are changed.
`SWEPH_PATH` remains an explicit override for tests/advanced use. With this override,
the GUI downloader and inventory are disabled to avoid managing an unused directory.
The native Swiss `SE_EPHE_PATH` variable is ignored, without changing the process
environment. The GUI reports this when the variable is present.
The CMake option still defaults to OFF.

## GUI download

Open Configuration (F2), Plugins, Solar System Editor, Configure, then the
**Swiss Ephemeris** tab. If the plugin is disabled, enable it at startup and restart.
Enter numbers/ranges (e.g. `1-1000`) and the first/last astronomical years
(year 0 means 1 BCE). Up to 10000 distinct numbers can be requested in one batch;
this is a batch-size safeguard, not a restriction to low-numbered asteroids.
Click **Download missing files**. Existing valid files are reused. Supporting
planet/lunar files and, when needed, the first-four-asteroid files are selected
in 600-year blocks. Individual asteroid files are always downloaded whole:
long first, short only after HTTP 404. File size and actual header coverage are
reported after download or inspection; there is no pre-download size catalogue.
The UI warns when an individual file does not cover the requested years.

Downloads run asynchronously, one at a time, with progress, cancellation, a
90-second request timeout and a 32 MiB per-file guard. Header filename, byte order,
length and finite coverage are checked before atomic installation. This is not
full binary validation: Swiss itself checks the remaining format when loading it.
Per-file URL, time, size, SHA-256 and coverage are recorded in `downloads.json`.
Completed files survive cancellation. After completion/cancellation the adapter
closes its files and clears state/availability caches under its mutex, allowing
new files to be used without restarting. Advance time to refresh drawn positions.
Missing data still use the existing Kepler fallback. Downloading data does not
import the asteroid into the object catalogue.

The Installed ephemerides dialog supports inventory and selective deletion (see below).
Forced updates of valid files and a pre-download coverage/size service are not yet implemented.

## Source dependency and ephemeris data

`util/fetch_swisseph.py` pins upstream revision
`aacf962d19d79f8bc921dbcdaacf306d85be1917`, preserves upstream license files,
and records source SHA-256 checksums in `build/swisseph-deps/manifest.json`.
These files stay in the ignored build directory. Use `--destination PATH` to choose
another dependency directory, then point CMake at `PATH/source`.
The old `--asteroids`, `--asteroids-only`, and `--length` options have been removed;
select asteroid numbers and coverage in the GUI instead. Existing data downloaded
by older versions, including their manifest receipts, are left in place.

Individual asteroid data in the GUI come from the mirror linked by the official
Swiss README: https://ephe.scryr.io/ephe/ . The GUI records download provenance in
the active profile's `downloads.json`; its data are separate from build dependencies.
For automated tests only, `util/prepare_swisseph_tests.py --download` fetches the
checksum-locked fixtures into `build/test-data/swiss`. It is not needed for a normal
build or for GUI downloads.

Actual file headers govern coverage. The installed Astraea file covers JD
625298.5–2817298.5, approximately 3000 BCE–3000 CE. The installed Eros **short** file
covers JD 2305442.5–2488342.5 (1599-12-27 to 2100-10-01, Gregorian, TT).
There is no long Eros file in the selected mirror at the time of this download.
In particular, Eros uses Kepler fallback in 1572 and 2990, while Astraea can use
Swiss at both dates. Nominal ranges such as 1500–2100 must not be treated as promises.

## Integration

`Planet::computeCoordinates()` is a common virtual hook for current positions,
off-date queries (including AstroCalc), orbit samples and parent state queries.
`MinorPlanet` overrides it only when the adapter successfully supplies a state.
The original Kepler object remains intact for fallback and orbital-element metadata.
Those elements, including the displayed period, are still approximate catalogue or
multi-epoch values; the prototype does not derive new osculating elements from Swiss.

The adapter requests geometric heliocentric ICRF equatorial Cartesian positions
and velocities in AU and AU/day. It passes TT JDE to `swe_calc()`. The caller uses
Stellarium's existing ICRF-to-VSOP87 rotation for both vectors. Stellarium remains
responsible for observer geometry and light-time/aberration corrections.

Swiss is compiled with TLSOFF and all access is serialized by one mutex, avoiding
platform-dependent thread-local initialization. The cache now keeps up to 16 exact
states and four one-minute interpolation segments per asteroid, for at most 16384
cached asteroid identities. This is a memory bound, not a supported-number cutoff.
Owners are evicted FIFO, and reload clears every cache. Failed exact lookups are
cached too. The adapter rejects silent Moshier fallback,
invalid/overflowing MPC numbers, nonfinite dates and failed/invalid file results.

Object extra information shows the active source and **asteroid file coverage**.
For the first four asteroids this is the currently loaded 600-year chunk, not the
union of every installed chunk. Scripting info maps add `position-source`;
Swiss results also include `ephemeris-start-jde`/`ephemeris-end-jde` (intersection
with currently loaded supporting files), `ephemeris-de-number`,
`asteroid-ephemeris-start-jde`/`asteroid-ephemeris-end-jde` (asteroid file itself),
and `ephemeris-file`. Missing data uses the existing
Kepler fallback, preserving its outdated-elements warning. Switching across missing
coverage can produce a position jump: no interpolation is fabricated across that gap.
Orbit drawing is suppressed near missing coverage if the required sample interval
cannot be supported. Within complete coverage it samples the integrated trajectory.

## Validation performed locally (2026-10-16)

- Full Release build of Stellarium with existing plugins.
- Adapter tests: modern and historical epochs, unsupported bodies, missing/disabled
  data, output unchanged on failure, velocity finite differences, file boundaries,
  pinned Horizons ICRF reference vectors and concurrent calls from eight threads.
- Approximately 12 ms for 10,000 sequential adapter calls on this host in one run.
  This is a four-asteroid microbenchmark, not a many-asteroid renderer benchmark.
- Running-app script `scripts/tests/testSwissEphemeris.ssc`: checks source selection
  and heliocentric distances at JD 1705425.5 (44 BCE March 15, Julian), JD 2295545.5
  (1572 November 11, Julian), and J2000, for all four objects; exercises orbit rendering,
  rapid forward/backward time and fallback in 3500 CE. The first two checks use TT
  midnight; they are date examples, not assertions of an historical event's exact time.
- A second app run with nonexistent ephemeris files checks usable Kepler fallback.
- Raw Horizons responses and numerical comparisons are under `build/validation`.
  Sixteen heliocentric comparisons at 1600, 1900, 2000 and 2100 showed roughly
  0.1–5.7 arcsec directional differences. These are not geocentric/topocentric
  pointing-error bounds. Horizons refused the earlier requests with a December
  1599 lower limit. Ancient absolute accuracy therefore remains unverified.

Run the app test with:

```sh
./util/run_swisseph.sh --full-screen no \
  --startup-script "$PWD/scripts/tests/testSwissEphemeris.ssc"
```

Its result is written to `build/user-swisseph/output.txt` (expect `failures=0`).
The test deliberately quits Stellarium. In the agent graphics environment, Qt needed
`LIBGL_ALWAYS_SOFTWARE=1 __GLX_VENDOR_LIBRARY_NAME=mesa QT_XCB_GL_INTEGRATION=xcb_glx`.
These are test-only environment settings, not changes to the desktop graphics setup.

## Before proposing upstream inclusion

Swiss Ephemeris uses AGPLv3 or a separate commercial agreement. Relevant Stellarium
files allow GPL-2.0-or-later, but **a complete dependency/license audit has not been
performed**. An enabled combined build needs an accepted GPLv3/AGPLv3 distribution
strategy or a suitable exception. Keeping the source outside the tree and defaulting
OFF do not remove those obligations when distributing an enabled build. Data-file
redistribution must also be addressed. No commercial license has been purchased.

Still needed: maintainer agreement on the interface and licensing, platform CI,
independent historical accuracy evidence, user settings/data management, automatic
inventory of installed coverage, broader sky-position and AstroCalc validation, and
tests of larger installed asteroid selections. The prototype does not replace Earth's
or the other planets' Stellarium ephemerides.

## Second-stage validation (2026-10-16)

- Four CTest tests pass, including long Astraea and short Eros files, explicit
  out-of-coverage rejection, alternating object identities, velocity checks,
  eight concurrent workers and independent Horizons reference vectors.
- Four network-free downloader tests cover numeric selection, filenames above
  99999, 404 fallback, server failures and preservation of existing files when
  a response is invalid. These were checks of the former CLI data downloader;
  `util/test_fetch_swisseph.py` now verifies the source-only fetcher instead.
- `scripts/tests/testSwissIndividualAsteroids.ssc` passes nine running-app checks
  of source and heliocentric distance plus mixed-source fallback checks in 1572;
  it also traverses the lower Eros file boundary in both time directions.
- Six heliocentric comparisons with Horizons at 1600, J2000 and 2050 give
  Astraea approximately 0.061–0.553 arcsec and Eros 0.0058–0.1154 arcsec.
  These remain heliocentric comparisons, not bounds on sky pointing errors.
- Raw responses, test outputs and fixtures are in `build/validation-step2/`.
- The user reports independent agreement against AstroGrav at arcsecond scale
  for Ceres, Vesta and Pallas, including around 2990 CE. This is user-supplied
  corroboration; the agent has not inspected the AstroGrav setup or reproduced it.

The individual-file test assumes the downloaded Astraea long / Eros short selection.
It is registered automatically when those files are present and no long Eros file
is installed. It must be revisited if a future Eros dataset extends the coverage.

## GUI-stage validation (2026-10-16)

- Full application build and five Swiss CTest checks pass.
- Offline header tests cover installed main/individual files, truncation, HTML,
  mismatched filenames, numeric ranges and numbers above 99999.
- An offscreen harness instantiates the actual downloader widget and presses its
  Download button against real upstream endpoints in an isolated user directory.
  It installs Astraea, Hebe and Eros with required supporting files, exercises
  long-to-short 404 fallback and coverage warnings, and verifies that Hebe becomes
  calculable after the previously cached missing-file result is invalidated.
- Cancellation during an active network request retains completed supporting files
  and does not install the interrupted asteroid file.
- The running application's nine individual-asteroid checks pass from the migrated
  test user directory. Evidence is in `build/validation-gui/`.

## 10000-asteroid performance work (2026-10-16)

The previous global 4096-state cache thrashed on a 10000-object sweep. It has been
replaced by bounded per-object working sets. The raw `calculate()` API remains
available; `MinorPlanet` now uses `calculateInterpolated()`. That path uses cubic
Hermite interpolation of exact heliocentric positions and velocities over at most
one TT minute. It differentiates the polynomial for velocity; it does not freeze
positions or omit light-time/aberration. Multiple cached intervals accommodate
geometric and light-time-corrected requests. Orbit samples and large date jumps
construct new intervals as required, so rapid time changes still have a cost.

Interpolation never extrapolates and is rejected when endpoints fail or have
different asteroid files, DE identifiers or effective coverage. Near file boundaries
it falls back to exact evaluation. The end of a file's valid interval is treated
as exclusive: the exact terminal date produced an anomalous Eros result in the
boundary test and must not be used as an interpolation endpoint. All accesses,
including segment construction and reload, are serialized. Set `SWEPH_EXACT=1`
before launching for direct Swiss results instead of interpolation.

Local measurements with the user's installed first-10000 data:

- Repeating an unchanged date: approximately 75 ms per 10000 direct requests before
  the cache change, 1–2 ms after (warm data).
- Changing the date by 0.1 seconds: approximately 78–80 ms for direct requests,
  0.25–0.46 ms for warm interpolation segments. Populating the first segments took
  approximately 166 ms. These are adapter timings, not whole-program FPS.
- 89985 valid comparisons at years 1600, 2000 and 2050 plus historical/file-boundary
  cases: maximum component position difference 7.11e-15 AU (about 1.1 mm), maximum
  component velocity difference 1.45e-11 AU/day. This quantifies added interpolation
  error in this sample only, not absolute ephemeris accuracy or a global guarantee.
- Six CTest cases pass, including reverse playback, big jumps, coverage edges,
  unchanged output on failure and concurrent calculation/reload.
- Full application builds. A new graphical integration run could not start because
  the current tool environment cannot connect to the display. No updated full-app
  FPS measurement is claimed. The earlier GUI-stage integration results predate
  this performance change.

Reproduce the large test with `SWEPH_PATH=<data-directory>
build/src/testSwissInterpolation --catalogue` (one shell command). Logs are under
`build/validation-performance/`.

Swiss mode explicitly requests `SEFLG_SWIEPH`, not `SEFLG_JPLEPH`: the compressed
`sepl_*.se1`/`semo_*.se1` supporting files downloaded by the GUI are used. A separate
full DE441 binary and Stellarium's `astro/de441_path` are not prerequisites for this
asteroid backend. Stellarium's observer/planet positions still use its own selected
planetary backend; configuring DE441 there is a separate accuracy choice, not a fix
for the asteroid cache/file-switching bottleneck.

## Inventory and deletion (2026-10-16)

Open the Swiss Ephemeris tab, then **Installed ephemerides…**. The dialog reads
4096-byte file headers in event-loop batches of 100, displaying number, name from
the file header, TT coverage, size and relative file path. Recognized individual
files and shared `sepl`/`semo`/`seas` files have separate groups. Number selections
(e.g. `1001-10000`) only select individual files, including both long and short
variants. Asteroids 1–4 share `seas` files and are managed in the shared group.
Columns can be sorted, including numeric number and size ordering.

Deletion requires confirmation of the selected file count and size, with an
additional explanation when shared files are selected. The operation permanently
deletes the files; it does not remove catalogue objects. It runs in batches of 64
with progress, reports failures, and updates `downloads.json` for successful
removals. The dialog cannot close mid-deletion. Downloads and inventory management
cannot be started concurrently through this UI.

The adapter holds its calculation mutex while closing Swiss files, clearing exact
and interpolation caches, and deleting a batch. It only accepts recognized relative
filenames beneath the configured user-data root, rejecting traversal, symlinks that
could escape the root and environment-path overrides. Unselected data remain intact.
Positions refresh on the next simulation update, even while time is paused. Files
can be downloaded again.
The test start helper does not seed or restore ephemeris data; use the GUI to download it.

Validation: seven CTest checks pass, including deletion from temporary fixture copies,
cache invalidation, retained asteroid/supporting files, removal of shared data,
traversal and symlink guards, and environment overrides. An offscreen Qt harness
exercises the actual inventory widget, numeric selection, rejecting confirmation,
confirming deletion, and subsequent calculation/fallback. Read-only inventory of
the real test profile found 10025 files in approximately 3.1 seconds. No real user
ephemerides were deleted. Logs/screenshots are in `build/validation-inventory/`.


## Planet cache invalidation (2026-10-16)

Swiss reloads increment an atomic data revision after clearing the adapter caches.
MinorPlanet exposes this revision to Planet, which invalidates both its current
position/velocity and cached orbit samples when the revision changes. Same-date
queries also bypass the current-position shortcut until that state is refreshed.
This covers downloads, deletion and explicit reloads without requiring the user to
advance simulation time. Other position providers retain revision zero.

`testSwissPlanetCache` runs the real application with an isolated temporary user
profile and copies of the four required fixture files (`sepl_18.se1`, `semo_18.se1`,
`seas_18.se1`, and `ast0/se00005.se1`). It deletes and reinstalls Astraea's file and
checks current position, velocity and orbit samples at identical dates. Build the
explicit target, then run from the source directory:

```
cmake --build build --target testSwissPlanetCache
build/src/testSwissPlanetCache "$PWD/build/swisseph-deps/ephe"
```

An OpenGL-capable display is required. `ENABLE_SWISSEPH_GUI_TESTS=ON` includes the
target in the default build and registers the integration test with CTest when the
fixture checks are satisfied. It defaults to OFF for headless builds.

Validation: the seven existing Swiss CTest checks and the full application test
passed. An isolated negative-control executable with orbit-cache invalidation
removed failed with `Stale orbit samples after deletion`, confirming that the test
detects the original orbit defect. Production source and libraries were not changed
for this negative control. All deletion tests used temporary copies; user files
were untouched. Logs are under `build/validation-cache-invalidation/`.


## Explicit ephemeris paths (2026-10-16)

The explicit adapter path is authoritative: the active user directory by default,
or the existing `SWEPH_PATH` developer override. Native `SE_EPHE_PATH` must not
redirect the library behind the adapter's and GUI's backs.

`cmake/SwissEphemeris.cmake` generates a build-local copy of `sweph.c`, replacing
only its `getenv("SE_EPHE_PATH")` expression with `NULL`. The external source stays
unchanged. CMake requires exactly one matching expression and stops for review if
that assumption changes. This small, explicit build patch avoids temporarily
changing the process environment while unrelated threads may be reading it. An
upstream API for an authoritative path could replace the patch in future.

Before calling Swiss, the adapter rejects paths exceeding its native 242-byte
argument limit, embedded NUL bytes, and search-path separators in the default
single directory. Explicit `SWEPH_PATH` search lists remain supported. Invalid
paths never reach the library's silent default-path fallback. The downloader
explains invalid paths and disables downloads; inventory/deletion of existing
user files remain available. Individual fully qualified filenames must also fit
Swiss's 255-byte limit: very deep directories can therefore still make individual
asteroid files unavailable even when the base path is accepted. Such failures
return to Kepler with the existing diagnostic, not to a different data directory.

The `swissPaths` regression uses temporary data and covers a conflicting native
environment variable, missing/empty explicit directories with valid native data,
the explicit developer override, spaces and Swedish characters, the 242/243-byte
boundary, separators and embedded NUL bytes. It checks actual source filenames
and that failures leave caller output unchanged. It also verifies that the native
environment variable is not modified. All eight Swiss CTest tests passed on Linux,
and Stellarium rebuilt successfully. Windows/macOS execution remains untested;
in particular this does not establish general Unicode support for Windows CRT
file I/O. Long paths are rejected, not made supported by this change.

## Position provenance (2026-10-16)

The source label and `getInfoMap()` now describe the stored current position,
including its retarded calculation time when light travel time is enabled. They
no longer request a new Swiss calculation at the observing date. A separate
`computeCurrentCoordinates()` hook records the successful Swiss result or Kepler
fallback while computing that position. Const off-date queries, coverage probes
and orbit samples use the existing coordinate hook and cannot replace this record.

The info map retains the existing source/file/coverage fields and adds
`position-source-jde` (the position's actual TT JDE) and
`position-source-revision` (the data revision captured for that position).
Swiss file and coverage fields are omitted for Kepler results. Before the first
stored position is computed, no source is claimed. Reloading data does not
retroactively relabel an old position; the next position update refreshes both
coordinates and their provenance, including at a paused simulation time.

The full-application regression `testSwissPlanetCache` also exercises both
coverage boundaries through `SolarSystem::computePositions()`, with light travel
time disabled and enabled. It checks that HTML and info-map source labels agree
with the retained position, that unrelated date queries do not overwrite source
metadata, and that the data revision changes only with the stored position.

Validation: Stellarium rebuilt and all eight Swiss CTest checks passed. The extended
full-application regression passed on Linux (exit 0), including the earlier
Astraea deletion/reinstallation checks. The Ceres boundary cases use 1800-01-01
and the fresh-file selection boundary at 2400-01-01; the raw files overlap this
latter date. The test warms the base info map's rise/set cache before resetting
Swiss state, because rise/set evaluation temporarily updates simulation dates.
At the first boundary the retarded position used Kepler; at the second it used
Swiss. Tests operate on temporary profiles and data copies. Logs are saved under
`build/validation-position-source/`.

## AstroCalc file-transition regression (2026-10-17)

`testSwissAstroCalc` initializes the real application in a temporary profile with
copies of `sepl`, `semo` and `seas` blocks 12 and 18. It operates the real AstroCalc
controls and Generate button, with the date-validity filter enabled. The table
row count and the shared `AstroCalcDialog::EphemerisList` are checked together.
SolarSystem's marker and line renderers use this list's `coord` values directly;
this is numerical verification of their inputs, not a pixel/screenshot test.

Twelve cases cover Ceres throughout 1799–1802 (147 points at ten-day spacing),
two days around 1800-01-01 (289 points at ten-minute spacing), and two days around
the older asteroid file's actual end (289 points). Each window is tested with
light time on/off and equatorial/horizontal coordinates: 2900 points total.
The older file overlaps into 1800; crossing a calendar block boundary does not
necessarily immediately close an already open file.

Each generated point is compared with the live simulation at the same JD in
reverse order. Exact raw Swiss states are checked at sampled broad-window dates
and every dense-window date, clearing Swiss state before the reference lookup.
The broad cases verify that reference lookups use both `seas_12.se1` and
`seas_18.se1`, and that no unexpected Kepler fallback occurs. All twelve cases
passed; maximum angular difference was 0.161034 arcsec, and maximum heliocentric
state difference from the raw reference was 1.80992e-15 AU. These are consistency
checks within Stellarium/Swiss, not an independent astronomical accuracy claim.

The horizontal comparison permits 1 arcsec; equatorial comparisons permit 1e-7
radians (0.021 arcsec). Stellarium's existing transformation caches reuse
precession for a day and nutation for an hour, so their epochs depend on traversal
order. A separate control executable linked a build-local precession.c copy with
both thresholds zero. All twelve cases passed there, with maximum angular
difference reduced to 0.00532361 arcsec. Production source/library caches were
unchanged. No Swiss integration change was required for this verification.

Build/run from the source directory:

```
cmake --build build --target testSwissAstroCalc
build/src/testSwissAstroCalc "$PWD/build/swisseph-deps/ephe"
```

An OpenGL display is required. With `ENABLE_SWISSEPH_GUI_TESTS=ON`, the target is
included in the default build and registered as `swissAstroCalc` when all six
fixture files exist. The eight non-GUI Swiss CTest checks also pass. Logs and the
isolated transformation-cache control are in `build/validation-astrocalc/`.
Only tests, test configuration and documentation changed in this step; user data
were untouched. Windows/macOS and pixel-level rendering remain untested.

## Batched download metadata (2026-10-17)

The downloader now uses `SwissEphemerisManifest`, a widget-independent metadata
store. Successful file installations update its in-memory object. It checkpoints
the complete `downloads.json` atomically with QSaveFile after 64 changed entries
or on a five-second timer while running, and flushes the final partial batch on
completion, cancellation and widget destruction. Identical entries and clean
flushes do not write. Ephemeris file installation remains immediate and atomic.
This reduces full-manifest rewrites, not their individual complexity; the JSON
format stays compatible with the inventory and command-line downloader.

A failed checkpoint retains dirty metadata for retry. The count threshold resets
on an attempt so a failure does not cause retries on every subsequent file. The
timer and final flush still retry. A failed final save disables inventory until
another download start can save pending metadata, avoiding stale metadata being
written back after inventory deletion. Shutdown attempts one final save and logs
any failure. Malformed/unreadable existing manifests stop a new run with an error
rather than being silently overwritten.

A crash can leave committed data files without their most recent manifest entries.
On the next run covering those files, the existing-file validation path registers
missing entries using size, SHA-256, coverage and `verified_utc`. It does not invent
an original `retrieved_utc`, URL or HTTP Last-Modified value. The files are reused
without downloading again. A previously saved entry retains its provenance.

Validation: `swissManifest` inserts 10000 records and checks 157 writes (156 full
batches and a final partial batch), compared with the previous 10000. Real runs
may add timed checkpoints. It also covers reload/resume, unchanged entries,
write failure with retained pending records, retry, and invalid JSON preservation.
All nine Swiss CTest checks passed. The full application test exercises the actual
downloader's timer callback, cancellation with and without a preceding checkpoint,
resume, reconstruction of missing entries and destruction with a partial batch.
These GUI checks use temporary installed fixtures, with no ephemeris network
requests. They run when SolarSystemEditor is enabled and explicitly report a skip
otherwise. Stellarium rebuilt successfully; logs are in `build/validation-manifest/`.


## Reproducible test coverage (2026-10-17)

`cmake/SwissTests.cmake` registers the same named suite regardless of feature,
fixture or GUI availability. CTest explicitly reports skipped backend/data/GUI
tests and gives the reason with `ctest -V`. A green summary can include skips;
check its "tests did not run" section. File/manifest tests and a real compiled-out
adapter test run even with `ENABLE_SWISSEPH=OFF`. `buildSwissTests` builds the
applicable targets and is also a dependency of the standard `buildTests` target.
The Python fixture-preparation tests require Python3; absence is reported as a skip.

The dedicated default fixture directory is `build/test-data/swiss`, separate from
user profiles and developer downloads. `src/tests/swiss-fixtures.lock` records
32 files (about 21 MB), exact sizes, SHA-256 hashes and retrieval URLs. Main files
are tied to Swiss commit aacf962d19d79f8bc921dbcdaacf306d85be1917; Astraea long and
Eros short files are checksum-pinned to the downloaded 2026-10-16 snapshot. Those
individual URLs are mutable: changed remote content fails validation, rather than
silently changing the tests. Data/licensing notices are not replaced by this lock.
No ephemeris binaries are stored in the source tree.

Prepare from existing local downloads, or explicitly request network retrieval:

```
python3 util/prepare_swisseph_tests.py --from-directory build/swisseph-deps/ephe
# Alternative, after reviewing upstream terms:
python3 util/prepare_swisseph_tests.py --download
```

With neither option the helper only verifies an already prepared directory. It
rejects unexpected `.se1` files (notably a long Eros file masking the short-file
test), validates before atomic replacement, and never updates the lock itself.
CMake/CTest never download files. Runtime checks detect missing, altered or extra
fixtures even if data change after configuration. Invalid data fail; missing data
are explicit skips unless strict mode is requested.

For the complete local/CI suite, enable both strict data and GUI tests:

```
cmake -S . -B build -DENABLE_TESTING=ON -DENABLE_SWISSEPH=ON \
  -DSWISSEPH_SOURCE_DIR="$PWD/build/swisseph-deps/source" \
  -DSWISSEPH_TEST_EPHE_PATH="$PWD/build/test-data/swiss" \
  -DSWISSEPH_REQUIRE_TEST_DATA=ON -DENABLE_SWISSEPH_GUI_TESTS=ON
cmake --build build --target buildSwissTests -j 6
ctest --test-dir build -R '^swiss' --output-on-failure
```

GUI tests require an OpenGL display; a Linux CI runner can use Xvfb and Mesa
software rendering. Strict mode rejects missing/invalid fixtures at configuration
and runtime, and rejects an accidentally disabled Swiss backend.

`.github/workflows/swiss-tests.yml` is a manually dispatched Linux/Qt6 matrix with
Swiss OFF and ON. It builds the application and applicable tests; the ON job
explicitly retrieves the pinned source and verified fixtures and enables the GUI
suite. It has read-only repository permissions, no automatic push/PR trigger and
no artifact-publication step. Review licensing before running or distributing
combined builds. This workflow has not been executed on GitHub in this session;
Windows/macOS verification remains future work.

Validation of the test infrastructure: a fresh Linux feature-OFF test build passed
its four data-independent checks and explicitly skipped nine backend-dependent
checks. A fresh feature-ON build without fixtures passed its applicable checks
and explicitly skipped the data/GUI cases. Enabling strict mode without fixtures
failed configuration as intended. After preparing the locked files, its non-GUI
suite passed. A deliberately corrupted fixture failed at runtime even in non-strict
mode, without being classified as a skip. The full local ON+strict+GUI run passed
all 13 CTest tests with no skips. The fixture preparer's offline tests cover verified
copy/resume, no implicit network access, extra-file rejection and preservation of
a target when source verification fails. Logs are in `build/validation-tests/`.
Fresh OFF/ON validation here built the test targets; a full fresh application build
for both modes is configured in the manual GitHub job but has not been run there.

## AstroCalc date-scroll regression (2026-10-18)

Opening AstroCalc connected hidden pages to date changes for the lifetime of the
window. Both What's Up Tonight (WUT) and the Almanac calculated while their page
or the entire dialog was hidden. The Almanac's rise/set calculations temporarily
update the whole solar system at other times, making this especially expensive
with thousands of installed asteroid ephemerides. Ephemeris markers themselves
retain their precomputed coordinates; this fix does not change Swiss positions.

WUT now calculates only on its visible page and refreshes on entry/reopening.
The Almanac skips hidden season/day calculations and refreshes through showEvent.
A duplicate AstroCalc visibility connection was also removed.

The real AstroCalc test checks that date signals leave sentinel values untouched
in both hidden pages, including with the dialog closed, and that both pages
refresh on entry, visible date changes and reopening. The WUT negative control
failed against the old implementation. Existing Ceres/file-boundary checks remain.

An optional local reproduction uses a copy of a profile's minor-body catalogue and
reads its ephemerides in place, without editing that profile. Example (Linux):

```sh
SWISS_SCROLL_PROFILE="$HOME/.stellarium" ./build/src/testSwissAstroCalc build/test-data/swiss
```

This mode generates Ganymed markers for January, year 8, then exercises daily date
signals. The tested catalogue contained 2242 solar-system objects (roughly 2000
imported asteroids). With only the WUT fix in place, ten daily steps still took
13788 ms with AstroCalc on the ephemeris page and 25582 ms with the dialog closed.
After also deferring the hidden Almanac, both batches measured below 1 ms. These
measurements cover synchronous date-change handlers, not frame rendering/FPS or
visible-almanac performance. Local logs are in `build/validation-scroll`.

Validation after the fix: Stellarium and Swiss test targets built successfully;
all 13 Swiss CTest tests passed (no skips), including the existing numerical
AstroCalc comparisons and the new hidden-page refresh checks. `git diff --check`
passed. No commits or uploads were made.

## Targeted off-date position updates (2026-10-18)

AstroCalc previously updated the complete imported asteroid catalogue for every
marker. Planet::getRTSTime also updated it at temporary dates, particularly during
Moon rise/transit/set iterations. These repeated catalogue sweeps are expensive
with the file-based, mutex-protected Swiss backend, even if only one object's
result is needed.

SolarSystem::ScopedPositionUpdate now provides temporary body/level lists for
synchronous off-date calculations. It retains non-MinorPlanet bodies, requested
minor planets, their ancestors and the outer scope's targets when nested. It
leaves the public catalogue unchanged and restores the previous update selection
on destruction. Both getRTSTime and getClosestRTSTime include the target and
observer; AstroCalc includes its primary/secondary targets, observer and any
opposition target. Asteroid epoch selection is restricted to the retained bodies.
AstroCalc also restores the touched bodies to the original date after generation.
The usual light-time, aberration and rotation calculations are reused unchanged.

This scope is for synchronous main-thread calculations only. Do not process GUI
events, render, mutate the catalogue or change observer inside it. Callers restore
the date and affected positions before leaving. Normal simulation updates still
process the full catalogue. It does not reduce interpolation precision or disable
Swiss calculations for requested objects.

Regression coverage checks an unrelated asteroid remains untouched, nested scopes
retain their targets, full updates resume afterwards, target coordinates agree,
and Moon/Mars/Ceres rise/transit/set results agree with a full-catalogue reference
with light-time both off and on. Existing file-boundary tests still compare marker
positions with the live sky. For their separate raw-output comparison, the info
panel's RTS cache is warmed and positions recalculated after a Swiss reload: both
asteroid and supporting files overlap at boundaries, and a cold exact call alone
can otherwise choose different valid files from those behind stored coordinates.
The numerical tolerances are unchanged.

Optional workload reproduction, using a copied catalogue and read-only ephemerides:

```sh
SWISS_WORKLOAD=1 SWISS_SCROLL_PROFILE="$HOME/.stellarium" \
  ./build/src/testSwissAstroCalc build/test-data/swiss
SWISS_WORKLOAD=1 SWISS_WORKLOAD_FULL=1 SWISS_SCROLL_PROFILE="$HOME/.stellarium" \
  ./build/src/testSwissAstroCalc build/test-data/swiss
```

The second command retains every catalogue body through an outer scope, providing
a full-catalogue reference without adding a production runtime switch. It generates
366 daily Ganymed markers starting January 1, astronomical year -1999 (2000 BCE),
and measures Moon/Mars RTS calls at that date. It prints a SHA-256 of all marker
coordinates/dates and the RTS results for comparison. Logs are saved locally in
`build/validation-workload`. This is calculation timing, not a rendering/FPS test.

Measured full-catalogue versus restricted work with the user's roughly 2000-asteroid
catalogue: 14484 ms versus 325 ms for the 366 markers; 353 ms versus 4 ms for Moon
RTS; 1 ms versus below 1 ms for Mars RTS. The SHA-256 of all marker coordinates and
dates was identical, as were all printed Moon/Mars RTS values (17 significant
digits). `comparison.json` records the comparison. Stellarium and all Swiss test
targets built successfully; all 13 CTest tests passed without skips, and
`git diff --check` passed. No commits or uploads were made.

### Hide asteroids without current ephemerides

The Swiss Ephemeris downloader tab has an opt-in **Hide asteroids without usable
ephemerides** checkbox. It persists as `astro/flag_hide_asteroids_without_ephemeris`
(default false). It filters MinorPlanet bodies, labels, orbits, trails, selection
pointers and spatial picking using the stored position source, including light-time
correction and data reload revisions. It performs no additional ephemeris queries.
Missing files, missing supporting files and uncovered dates therefore hide the body;
covered positions reappear after the normal position update. Planets, moons and
comets are unaffected. Builds without Swiss support ignore the display filter.
Catalogue/name searches and previously calculated AstroCalc markers remain available.
Trail history is retained and becomes visible again when the current position is
covered; this is a current-position filter, not a validation of historical trail points.
