# Separate profile and upstream updates

Start this build on Linux with `./util/run_swisseph.sh`. It uses
`~/.stellarium-swisseph` for settings, imported objects and downloaded ephemerides.
The normal `~/.stellarium` profile is independent. Directly launching the binary
without `--user-dir` still selects Stellarium's normal profile.

The helper does not copy a profile automatically. An existing profile can be copied
once while Stellarium is closed; use independent copies, not shared symlinks or
hard links. Subsequent imports, downloads and deletions then affect only the
selected profile. Ephemerides live under `ephemeris/swiss` inside that profile.
Use `SWISSEPH_USER_DIR` to select another directory with the helper.

## Repository setup

The project repository is `https://github.com/AmsesBobo/stellarium-swisseph`.
Keep its development branch as `swisseph`. Preserve the original Stellarium
history so updates can be merged normally. Configure `origin` for this repository
and `upstream` for `https://github.com/Stellarium/stellarium.git`.

On a fresh clone, add upstream once:

```sh
git remote add upstream https://github.com/Stellarium/stellarium.git
```

With a clean working tree, review upstream changes before merging:

```sh
git switch swisseph
git fetch upstream
git log --oneline HEAD..upstream/master
git merge upstream/master
```

Resolve any conflicts, rebuild using the Swiss CMake options in the prototype
document, and run the relevant tests before `git push origin swisseph`.
Fetching upstream does not change the working tree. Merging is deliberately a
manual step: this experimental integration may conflict with future upstream code.
Do not force-push upstream merges or replace existing copyright attribution.

Use repository-local Git author settings appropriate to the publishing account;
these do not change the identity used in other working copies.
