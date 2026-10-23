# Licensing of the Swiss Ephemeris experiment

Integration and publication notes, 2026-10-23.

Stellarium retains its existing copyright notices and GPL-2.0-or-later terms
(see [CREDITS.md](../CREDITS.md) and [COPYING](../COPYING)). New integration code,
tests and helper scripts are provided under GPL-2.0-or-later. Existing third-party
components retain their own licenses. This is a modified experimental version.

Swiss Ephemeris is an external dependency, pinned to
`aacf962d19d79f8bc921dbcdaacf306d85be1917`. Its upstream license offers AGPLv3
or a separate professional license. The source fetcher preserves its license
notices. The optional build uses the AGPL route; GPLv3 and AGPLv3 section 13
provide for combining code under these licenses, with GPLv3 chosen for compatible
GPL-2.0-or-later parts. Downloading Swiss separately does not remove the licensing
obligations of a combined executable.

The build generates a patched copy of `sweph.c` that disables the native
`SE_EPHE_PATH` environment override, leaving Stellarium in control of its data
directory. The exact transformation is in `cmake/SwissEphemeris.cmake`.
Swiss is linked statically with TLS disabled; calls are serialized by the adapter.

This repository publishes integration source, scripts and documentation only.
It does not include the downloaded Swiss source, ephemerides, personal profiles
or compiled binaries. Source and data downloads have separate provenance records.
Before distributing a Swiss-enabled executable, review all concrete dependencies
and provide the required notices, license texts and complete corresponding source,
including Swiss and the build-time modification. Binary packaging is not covered
by this experimental repository's validation. Data redistribution is not authorized
merely by the presence of a download button.

Primary references:

- [Pinned Swiss license](https://github.com/aloistr/swisseph/blob/aacf962d19d79f8bc921dbcdaacf306d85be1917/LICENSE)
- [Swiss licensing information](https://www.astro.com/swisseph/swephinfo_e.htm)
- [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)
- [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.html)
- [GNU compatibility explanation](https://www.gnu.org/licenses/gpl-faq.html#AGPLGPL)
