/* Stellarium — experimental asteroid ephemeris adapter.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef STELLARIUM_SWISS_EPHEMERIS_HPP
#define STELLARIUM_SWISS_EPHEMERIS_HPP
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace SwissEphemeris
{
struct State
{
    // Geometric heliocentric ICRF position (AU) and velocity (AU/day).
    std::array<double, 3> position{};
    std::array<double, 3> velocity{};
    double startJDE = 0.;
    double endJDE = 0.;
    int deNumber = 0;
    // Raw asteroid file coverage, before intersection with supporting files.
    double asteroidStartJDE = 0.;
    double asteroidEndJDE = 0.;
    std::string filePath;
};
//! Configure the default user-data directory at startup. SWEPH_PATH overrides it.
void setDataDirectory(const std::string& path);
//! Close files and invalidate cached states after installing data. Thread-safe.
void reload();
//! Monotonic version for caches outside this adapter; changes on reload/removal.
std::uint64_t dataRevision();
//! Delete recognized data files relative to the configured user directory.
//! Returns failed paths. Closes files and clears all caches under the calculation
//! mutex. Refuses environment overrides, traversal, and symlinks outside the root.
std::vector<std::string> removeFiles(const std::vector<std::string>& paths);
enum class PathStatus { Ready, Empty, TooLong, Invalid };
//! Validate the effective native-byte path before calling Swiss. SE_EPHE_PATH is
//! ignored by our library build; SWEPH_PATH remains an explicit developer override.
PathStatus pathStatus();
//! Enabled only in opt-in builds with a usable configured data directory.
bool isEnabled();
//! Thread-safe. Numbered asteroids with installed main or individual .se1 files.
//! JDE is TT; no light-time, aberration, nutation or precession is applied.
//! False leaves output untouched; the caller must explicitly select a fallback.
bool calculate(int asteroidNumber, double jde, State& output);
//! Cubic Hermite interpolation of exact states over at most one minute (TT).
//! Uses exact calculation at file boundaries; never extrapolates. SWEPH_EXACT=1
//! selects the raw-library path for reproducible comparisons.
bool calculateInterpolated(int asteroidNumber, double jde, State& output);
}
#endif
