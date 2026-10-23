/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SwissEphemeris.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <vector>
#include <cstdlib>

static void require(bool ok, const char* message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc,argv);
    using namespace SwissEphemeris;
    const bool catalogue=app.arguments().contains("--catalogue");
    std::vector<int> numbers{1,2,3,4,5,433};
    if (catalogue) { numbers.clear(); for (int n=1;n<=10000;++n) numbers.push_back(n); }
    double maxPosition=0., maxVelocity=0.;
    int checked=0;
    const auto compare=[&](int number,double jd) {
        State direct, interpolated; interpolated.position[0]=123.;
        const bool a=calculate(number,jd,direct), b=calculateInterpolated(number,jd,interpolated);
        require(a==b,"Interpolation changed data availability");
        if (!a) { require(interpolated.position[0]==123.,"Failure modified caller output"); return; }
        require(direct.filePath==interpolated.filePath,"Interpolation changed asteroid identity/file");
        require(interpolated.startJDE<=jd && interpolated.endJDE>=jd,"Invalid reported coverage");
        for (int k=0;k<3;++k)
        {
            if (std::abs(direct.position[k]-interpolated.position[k])>1e-10)
                std::cerr << "position mismatch number=" << number << " jd=" << std::fixed << jd << " component=" << k << " delta=" << std::scientific << direct.position[k]-interpolated.position[k] << '\n';
            maxPosition=std::max(maxPosition,std::abs(direct.position[k]-interpolated.position[k]));
            maxVelocity=std::max(maxVelocity,std::abs(direct.velocity[k]-interpolated.velocity[k]));
        }
        ++checked;
    };
    for (double epoch : {2305447.5,2451545.,2469807.5})
        for (int n : numbers)
            for (double seconds : {4.,29.,57.}) compare(n,epoch+seconds/86400.);
    // Reverse playback, large jumps, main-file boundaries and short-file edges.
    for (double epoch : {2813135.5,1705425.5,2378496.5,2305442.5,2488342.5})
        for (int n : {1,5,433})
            for (double seconds : {1.,0.,-1.}) compare(n,epoch+seconds/86400.);
    require(checked>0,"No installed data for interpolation test");
    require(maxPosition<1e-10,"Interpolation exceeds 1e-10 AU component tolerance");
    require(maxVelocity<1e-8,"Interpolation velocity exceeds tolerance");
    State sentinel; sentinel.position[0]=123.;
    require(!calculateInterpolated(0,2451545.,sentinel),"Invalid number accepted");
    require(!calculateInterpolated(1,std::numeric_limits<double>::quiet_NaN(),sentinel),"NaN accepted");
    require(sentinel.position[0]==123.,"Invalid input modified output");
    std::vector<std::future<bool>> workers;
    for (int t=0;t<8;++t) workers.push_back(std::async(std::launch::async,[t] {
        for (int i=0;i<100;++i)
        {
            State a,b;
            const int n=t%2 ? 5 : 433;
            const double jd=2451545.+(i*.1+t)/86400.;
            if (!calculateInterpolated(n,jd,a) || !calculate(n,jd,b)) return false;
            for (int k=0;k<3;++k) if(std::abs(a.position[k]-b.position[k])>1e-10) return false;
            if (i%25==0) reload();
        }
        return true;
    }));
    for(auto& worker:workers) require(worker.get(),"Concurrent interpolation/reload failed");
    std::cout<<"checked="<<checked<<" max_position_component_AU="<<maxPosition
             <<" max_velocity_component_AU_day="<<maxVelocity<<'\n';
    if (catalogue)
        for (bool fast : {false,true})
        {
            reload();
            for (int frame=0;frame<5;++frame)
            {
                QElapsedTimer timer; timer.start(); int valid=0;
                for (int n:numbers)
                {
                    State s; const double jd=2451545.+(10.+frame*.1)/86400.;
                    valid+=fast ? calculateInterpolated(n,jd,s) : calculate(n,jd,s);
                }
                std::cout<<"interpolated="<<fast<<" frame="<<frame<<" ms="
                         <<timer.nsecsElapsed()/1e6<<" available="<<valid<<'\n';
            }
        }
}
