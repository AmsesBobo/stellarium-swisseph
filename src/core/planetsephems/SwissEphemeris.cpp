/* Stellarium — experimental asteroid ephemeris adapter.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "SwissEphemeris.hpp"
#ifdef ENABLE_SWISSEPH
#include "swephexp.h"
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <limits>
#include <deque>
#include <unordered_map>
#include <atomic>

namespace
{
QByteArray defaultPath;
std::atomic<std::uint64_t> revision{1};
QByteArray ephemerisPath()
{
    const QByteArray overridePath = qgetenv("SWEPH_PATH");
    return overridePath.isEmpty() ? defaultPath : overridePath;
}
struct CachedState { bool valid; SwissEphemeris::State state; };
struct Segment
{
    double start, end;
    SwissEphemeris::State first, last;
};
struct ObjectCache
{
    std::deque<Segment> segments;
    std::deque<std::pair<double, CachedState>> states;
};
struct Context
{
    std::recursive_mutex mutex;
    bool initialized = false;
    bool pathWarningIssued = false;
    std::set<int> warned;
    std::map<int, bool> installed;
    // Each asteroid keeps its own working set. A catalogue sweep must not
    // evict every previous asteroid before the next frame (4096 global entries
    // caused exactly that with 10000 objects). Bound the number of owners too.
    std::unordered_map<int, ObjectCache> cache;
    std::deque<int> owners;
    ~Context() { if (initialized) swe_close(); }
};
Context& context() { static Context c; return c; }

// Called under the context mutex. Cache absent files too: hundreds of catalogue
// asteroids without installed data must not cause file I/O every frame.
bool hasInstalledFile(Context& c, int number)
{
    if (number <= 4) return true; // Native seas_* files are selected by date.
    auto found = c.installed.find(number);
    if (found != c.installed.end()) return found->second;
    const QString stem = number <= 99999
        ? QString("se%1").arg(number, 5, 10, QLatin1Char('0'))
        : QString("s%1").arg(number, 6, 10, QLatin1Char('0'));
#ifdef Q_OS_WIN
    const auto paths = QString::fromLocal8Bit(ephemerisPath()).split(QLatin1Char(';'), Qt::SkipEmptyParts);
#else
    const auto paths = QString::fromLocal8Bit(ephemerisPath()).split(QRegularExpression("[;:]"), Qt::SkipEmptyParts);
#endif
    bool present = false;
    for (const QString& path : paths)
        for (const QString& prefix : {QString("ast%1/").arg(number/1000), QString()})
            for (const QString& suffix : {QString(".se1"), QString("s.se1")})
                present = present || QFileInfo(QDir(path).filePath(prefix+stem+suffix)).isFile();
    c.installed.emplace(number, present);
    return present;
}
}
#endif

void SwissEphemeris::setDataDirectory(const std::string& path)
{
#ifdef ENABLE_SWISSEPH
    // Startup only, before worker threads or the first position calculation.
    defaultPath = QByteArray::fromStdString(path);
#else
    (void)path;
#endif
}

void SwissEphemeris::reload()
{
#ifdef ENABLE_SWISSEPH
    auto& c = context();
    std::lock_guard<std::recursive_mutex> lock(c.mutex);
    if (c.initialized) swe_close();
    c.initialized = false;
    c.cache.clear();
    c.owners.clear();
    c.installed.clear();
    c.warned.clear();
    c.pathWarningIssued = false;
    revision.fetch_add(1, std::memory_order_release);
#endif
}

std::uint64_t SwissEphemeris::dataRevision()
{
#ifdef ENABLE_SWISSEPH
    return revision.load(std::memory_order_acquire);
#else
    return 0;
#endif
}

std::vector<std::string> SwissEphemeris::removeFiles(const std::vector<std::string>& paths)
{
#ifdef ENABLE_SWISSEPH
    auto& c = context();
    std::lock_guard<std::recursive_mutex> lock(c.mutex);
    if (!qgetenv("SWEPH_PATH").isEmpty() || defaultPath.isEmpty()) return paths;
    const QString root=QFileInfo(QString::fromLocal8Bit(defaultPath)).canonicalFilePath();
    if (root.isEmpty()) return paths;
    const QRegularExpression allowed("^(?:ast[0-9]+/(?:se[0-9]{5}|s[0-9]{6,})s?|(?:sepl|semo|seas)[_m][0-9]{2})\\.se1$");
    reload(); // recursive mutex keeps workers out until removal is complete.
    std::vector<std::string> failed;
    for (const auto& path:paths)
    {
        const QString relative=QString::fromUtf8(path.c_str());
        const QFileInfo file(QDir(root).filePath(relative));
        if (!allowed.match(relative).hasMatch() || file.isSymLink() || !file.isFile() ||
            !file.canonicalFilePath().startsWith(root+"/") || !QFile::remove(file.absoluteFilePath()))
            failed.push_back(path);
    }
    return failed;
#else
    return paths;
#endif
}

SwissEphemeris::PathStatus SwissEphemeris::pathStatus()
{
#ifdef ENABLE_SWISSEPH
    const QByteArray path = ephemerisPath();
    if (path.isEmpty()) return PathStatus::Empty;
    // swe_set_ephe_path silently substitutes its default above this limit.
    if (path.size() > AS_MAXCH-1-13) return PathStatus::TooLong;
    if (path.contains('\0')) return PathStatus::Invalid;
    // The default is one directory, never a Swiss search-path list. Preserve
    // lists only for the explicit developer SWEPH_PATH override.
    if (qgetenv("SWEPH_PATH").isEmpty() &&
        (path.contains(';')
#ifndef Q_OS_WIN
         || path.contains(':')
#endif
        )) return PathStatus::Invalid;
    return PathStatus::Ready;
#else
    return PathStatus::Empty;
#endif
}

bool SwissEphemeris::isEnabled()
{
#ifdef ENABLE_SWISSEPH
    return pathStatus() == PathStatus::Ready;
#else
    return false;
#endif
}

bool SwissEphemeris::calculate(int number, double jde, State& output)
{
#ifdef ENABLE_SWISSEPH
    if (number < 1 || number == 134340 || number > std::numeric_limits<int32>::max()-SE_AST_OFFSET || !std::isfinite(jde))
        return false;
    auto& c = context();
    std::lock_guard<std::recursive_mutex> lock(c.mutex);
    const auto status = pathStatus();
    if (status != PathStatus::Ready)
    {
        if (status != PathStatus::Empty && !c.pathWarningIssued)
        {
            qWarning() << "Swiss Ephemeris: invalid or too long data path; using Kepler elements."
                       << "Maximum path length:" << AS_MAXCH-1-13 << "bytes.";
            c.pathWarningIssued = true;
        }
        return false;
    }
    if (!hasInstalledFile(c, number)) return false;
    if (!c.initialized)
    {
        swe_set_ephe_path(ephemerisPath().constData());
        c.initialized = true;
        qInfo() << "Swiss Ephemeris: numbered asteroids with installed data; path" << ephemerisPath();
    }
    auto found = c.cache.find(number);
    if (found == c.cache.end())
    {
        if (c.cache.size() >= 16384)
        {
            c.cache.erase(c.owners.front());
            c.owners.pop_front();
        }
        found = c.cache.emplace(number, ObjectCache{}).first;
        c.owners.push_back(number);
    }
    auto& states = found->second.states;
    for (auto it=states.rbegin(); it!=states.rend(); ++it)
    {
        if (it->first != jde) continue;
        if (it->second.valid) output = it->second.state;
        return it->second.valid;
    }
    constexpr int flags = SEFLG_SWIEPH | SEFLG_HELCTR | SEFLG_TRUEPOS | SEFLG_J2000
        | SEFLG_ICRS | SEFLG_EQUATORIAL | SEFLG_XYZ | SEFLG_SPEED | SEFLG_NONUT;
    double coordinates[6] = {};
    char error[AS_MAXCH] = {};
    const int result = swe_calc(jde, number <= 4 ? SE_CERES+number-1 : SE_AST_OFFSET+number, flags, coordinates, error);
    // Never advertise a silent Moshier substitution as a file-based ephemeris.
    bool valid = result >= 0 && (result & (SEFLG_SWIEPH | SEFLG_MOSEPH | SEFLG_JPLEPH)) == SEFLG_SWIEPH;
    State state;
    if (valid)
    {
        const char* file = swe_get_current_file_data(number <= 4 ? 2 : 3, &state.startJDE, &state.endJDE, &state.deNumber);
        valid = file && jde >= state.startJDE && jde < state.endJDE;
        state.filePath = file ? file : "";
        state.asteroidStartJDE = state.startJDE;
        state.asteroidEndJDE = state.endJDE;
        // Include the actual coverage of supporting planetary/lunar files.
        for (int i=0; i<2 && valid; ++i)
        {
            double start=0., end=0.; int de=0;
            if (swe_get_current_file_data(i, &start, &end, &de))
            {
                state.startJDE = std::max(state.startJDE, start);
                state.endJDE = std::min(state.endJDE, end);
            }
        }
        valid = valid && jde >= state.startJDE && jde < state.endJDE;
        for (int i=0; i<3; ++i)
        {
            state.position[i] = coordinates[i];
            state.velocity[i] = coordinates[i+3];
            valid = valid && std::isfinite(coordinates[i]) && std::isfinite(coordinates[i+3]);
        }
    }
    if (!valid && c.warned.insert(number).second)
    {
        qWarning() << "Swiss Ephemeris unavailable for asteroid" << number << "at JDE" << jde
                   << "- using Kepler elements:" << error;
    }
    // Keep light-time iterations and recent dates; orbit samples cannot
    // displace the working sets of unrelated asteroids.
    if (states.size() >= 16) states.pop_front();
    states.emplace_back(jde, CachedState{valid, state});
    if (valid) output = state;
    return valid;
#else
    (void)number; (void)jde; (void)output;
    return false;
#endif
}


bool SwissEphemeris::calculateInterpolated(int number, double jde, State& output)
{
#ifdef ENABLE_SWISSEPH
    // Exact mode is useful for comparison and for applications requiring raw
    // library output. Read once: do not query the environment for every object.
    static const bool exact = qEnvironmentVariableIntValue("SWEPH_EXACT") != 0;
    if (exact || !std::isfinite(jde)) return calculate(number,jde,output);
    auto& c = context();
    // Recursive because segment endpoints use the same exact path and mutex.
    // reload() cannot invalidate either endpoint while a segment is constructed.
    std::lock_guard<std::recursive_mutex> lock(c.mutex);
    const auto interpolate = [jde,&output](const Segment& segment) {
        const double h=segment.end-segment.start;
        const double u=(jde-segment.start)/h;
        const double u2=u*u, u3=u2*u;
        output=segment.first;
        output.startJDE=std::max(segment.first.startJDE,segment.last.startJDE);
        output.endJDE=std::min(segment.first.endJDE,segment.last.endJDE);
        for (int k=0;k<3;++k)
        {
            const double p=segment.first.position[k];
            const double delta=segment.last.position[k]-p;
            const double v0=segment.first.velocity[k], v1=segment.last.velocity[k];
            // Difference form avoids cancellation between large absolute positions.
            output.position[k]=p+(-2*u3+3*u2)*delta+(u3-2*u2+u)*h*v0+(u3-u2)*h*v1;
            output.velocity[k]=(-6*u2+6*u)*delta/h+(3*u2-4*u+1)*v0+(3*u2-2*u)*v1;
        }
        return true;
    };
    auto found=c.cache.find(number);
    if (found!=c.cache.end())
        for (auto it=found->second.segments.rbegin(); it!=found->second.segments.rend(); ++it)
            if (jde>=it->start && jde<=it->end) return interpolate(*it);

    // One-minute TT intervals. No extrapolation, no interpolation across a file
    // boundary or a failed endpoint. A jump in time constructs a fresh segment.
    const double start=2451545.+std::floor((jde-2451545.)*1440.)/1440.;
    const double end=start+1./1440.;
    if (!(start<=jde && jde<=end)) return calculate(number,jde,output);
    State a,b;
    if (!calculate(number,start,a) || !calculate(number,end,b) ||
        a.filePath!=b.filePath || a.deNumber!=b.deNumber ||
        a.startJDE!=b.startJDE || a.endJDE!=b.endJDE ||
        start<=std::max(a.startJDE,b.startJDE) || end>=std::min(a.endJDE,b.endJDE))
        return calculate(number,jde,output);
    auto& segments=c.cache.at(number).segments;
    // Geometric and retarded positions usually lie in two different intervals.
    // Several intervals also accommodate observer motion and backwards playback.
    if (segments.size()>=4) segments.pop_front();
    segments.push_back(Segment{start,end,a,b});
    return interpolate(segments.back());
#else
    (void)number; (void)jde; (void)output;
    return false;
#endif
}
