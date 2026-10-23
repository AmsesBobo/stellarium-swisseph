/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SwissEphemeris.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <iostream>
#include <future>
#include <limits>
#include <cmath>
#include <vector>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using SwissEphemeris::State;
    using SwissEphemeris::calculate;
    const auto args = app.arguments();
    auto require = [](bool ok, const char* message) {
        if (!ok) { std::cerr << message << '\n'; std::exit(1); }
    };
    QList<int> dumpNumbers{1,2,3,4};
    if (args.contains("--numbers"))
    {
        dumpNumbers.clear();
        for (const QString& value : args.value(args.indexOf("--numbers")+1).split(','))
        {
            bool ok=false; const int number=value.toInt(&ok);
            require(ok && number > 0, "Invalid --numbers list");
            dumpNumbers.append(number);
        }
    }
    if (args.contains("--dump"))
    {
        QJsonArray rows;
        for (int i=args.indexOf("--dump")+1; i<args.size(); ++i)
        {
            const double jde=args[i].toDouble();
            for (int n : dumpNumbers)
            {
                State s;
                require(calculate(n,jde,s), "No ephemeris for dump date");
                QJsonArray p, v;
                for (int k=0;k<3;++k) { p.append(s.position[k]); v.append(s.velocity[k]); }
                rows.append(QJsonObject{{"number",n},{"jde",jde},{"position",p},{"velocity",v},
                    {"start",s.startJDE},{"end",s.endJDE},{"de",s.deNumber},
                    {"asteroidStart",s.asteroidStartJDE},{"asteroidEnd",s.asteroidEndJDE},
                    {"file",QString::fromStdString(s.filePath)}});
            }
        }
        std::cout << QJsonDocument(rows).toJson().constData();
        return 0;
    }
    State sentinel; sentinel.position[0]=123.;
    if (args.contains("--unavailable"))
    {
        require(!calculate(1,2451545.,sentinel), "Missing data must fail explicitly");
        require(sentinel.position[0]==123., "Failure changed caller output");
        return 0;
    }
    require(SwissEphemeris::isEnabled(), "Set SWEPH_PATH to downloaded data");
    require(!calculate(0,2451545.,sentinel), "Unsupported asteroid accepted");
    require(!calculate(1,std::numeric_limits<double>::quiet_NaN(),sentinel), "NaN accepted");
    require(!calculate(1,0.,sentinel), "Out-of-range date accepted");
    require(sentinel.position[0]==123., "Failure changed caller output");
    require(!calculate(std::numeric_limits<int>::max(),2451545.,sentinel), "Overflowing ID accepted");
    if (args.contains("--extended"))
    {
        // Independent JPL Horizons ICRF heliocentric vectors, TT J2000,
        // same query settings as the main-asteroid reference below (2026-10-16).
        const double individualReference[2][3] = {
            {-2.149984507247657, -0.6284405511655514, -0.08689266871927352},
            {-1.197012139611607, -0.4156964432142275, -0.4522312735558778}
        };
        for (int n : {5,433})
        {
            State referenceState;
            require(calculate(n,2451545.,referenceState), "Individual reference epoch unavailable");
            double squaredError=0.;
            for(int k=0;k<3;++k)
                squaredError+=std::pow(referenceState.position[k]-individualReference[n==5 ? 0 : 1][k],2);
            require(std::sqrt(squaredError)<1e-5, "Individual state differs from Horizons reference");
            for (double jde : {2305447.5,2451545.,2469807.5})
            {
                State first, other, again, before, after;
                require(calculate(n,jde,first), "Separate asteroid file unavailable");
                // Swiss has one slot for arbitrary asteroids: switching must not mix states.
                require(calculate(n==5 ? 433 : 5,jde,other), "Second asteroid unavailable");
                require(calculate(n,jde,again) && first.position==again.position,
                        "Asteroid identity lost after switching files");
                double distance=0.;
                for(int k=0;k<3;++k) distance+=std::pow(first.position[k]-other.position[k],2);
                require(distance>.01, "Distinct asteroids returned the same state");
                require(first.startJDE<=jde && first.endJDE>=jde, "Incorrect individual file coverage");
                require(calculate(n,jde-.001,before) && calculate(n,jde+.001,after), "Velocity samples failed");
                for(int k=0;k<3;++k)
                    require(std::abs((after.position[k]-before.position[k])/.002-first.velocity[k])<1e-8,
                            "Individual asteroid velocity inconsistent");
            }
        }
        State longState;
        require(calculate(5,1705425.5,longState) && calculate(5,2813135.5,longState),
                "Astraea long file not used outside short-file period");
        require(!calculate(433,1705425.5,sentinel) && !calculate(433,2295545.5,sentinel) && !calculate(433,2813135.5,sentinel),
                "Eros short file extrapolated beyond coverage");
        require(sentinel.position[0]==123., "Individual file failure changed output");
        std::vector<std::future<bool>> tasks;
        for(int t=0;t<8;++t) tasks.push_back(std::async(std::launch::async,[t] {
            for(int i=0;i<300;++i) {
                State a,b; const int n=t%2 ? 5 : 433;
                const double jd=2451545.+i*.13+t*.007;
                if(!calculate(n,jd,a) || !calculate(n,jd,b) || a.position!=b.position) return false;
            }
            return true;
        }));
        for(auto& task:tasks) require(task.get(), "Individual file concurrency failed");
        std::cout << "PASS: separate long/short files, object identity, coverage, velocity, 8 threads\n";
    }
    // JPL Horizons, retrieved 2026-10-16: numbered asteroids 1..4; CENTER=500@10,
    // VECTORS, REF_SYSTEM=ICRF, REF_PLANE=FRAME, VEC_CORR=NONE, AU-D, TT JD2451545.
    // Independent reference with tolerance for differences in the orbit solutions.
    const double reference[4][3] = {
        {-2.379327714653171, 0.5456711064170952, 0.7412254705021675},
        {-0.841138452027973, 1.944444964159562, -0.3274545807510458},
        {0.3841772400975993, -3.039797445723759, -0.586933292529939},
        {-1.353580420332906, -1.620555914081965, -0.4683669911569083}
    };
    for(int n=1;n<=4;++n)
    {
        State state;
        require(calculate(n,2451545.,state), "Reference epoch unavailable");
        double squaredError=0.;
        for(int k=0;k<3;++k) squaredError+=std::pow(state.position[k]-reference[n-1][k],2);
        require(std::sqrt(squaredError)<1e-5, "Heliocentric ICRF vector differs from Horizons reference");
    }
    const double dates[]={1705425.5,2295545.5,2451545.,2488070.};
    for (double jd:dates) for (int n=1;n<=4;++n)
    {
        State s, before, after;
        require(calculate(n,jd,s), "Historical state unavailable");
        require(s.deNumber==441, "Unexpected data version");
        require(s.startJDE<=jd && s.endJDE>=jd, "Invalid coverage metadata");
        require(calculate(n,jd-.01,before) && calculate(n,jd+.01,after), "Velocity samples failed");
        for(int k=0;k<3;++k)
            require(std::abs((after.position[k]-before.position[k])/.02-s.velocity[k])<1e-8,
                    "Velocity inconsistent with position (AU/day)");
        // Sweep each nearby file boundary: a discontinuity must not hide at a chunk join.
        for(double boundary: {s.startJDE,s.endJDE})
        {
            State a,b;
            if(calculate(n,boundary-.0001,a) && calculate(n,boundary+.0001,b))
                for(int k=0;k<3;++k)
                    require(std::abs(b.position[k]-a.position[k]-a.velocity[k]*.0002)<1e-8,
                            "Discontinuity at ephemeris file boundary");
        }
    }
    std::vector<std::future<bool>> workers;
    for(int t=0;t<8;++t) workers.push_back(std::async(std::launch::async,[t] {
        for(int i=0;i<200;++i) {
            State a,b; const double jd=2451545.+i*.125+t*.01;
            if(!calculate(t%4+1,jd,a) || !calculate(t%4+1,jd,b) || a.position!=b.position) return false;
        }
        return true;
    }));
    for(auto& worker:workers) require(worker.get(), "Concurrent calculation failed");
    QElapsedTimer timer; timer.start();
    for(int i=0;i<10000;++i) { State s; require(calculate(i%4+1,2400000.+i*.02,s), "Benchmark failed"); }
    std::cout << "PASS: historical states, missing data, coverage, velocity, chunk boundaries, 8 threads. "
              << "10000 states: " << timer.elapsed() << " ms\n";
}
