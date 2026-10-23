/* SPDX-License-Identifier: GPL-2.0-or-later */
// Exercise the actual compiled-out adapter even in a Swiss-enabled build.
#undef ENABLE_SWISSEPH
#include "../core/planetsephems/SwissEphemeris.cpp"
#include <iostream>
int main()
{
    SwissEphemeris::setDataDirectory("not-a-real-directory");
    SwissEphemeris::reload();
    SwissEphemeris::State state; state.position[0]=123.;
    const std::vector<std::string> files{"ast0/se00005.se1"};
    if(SwissEphemeris::isEnabled() || SwissEphemeris::dataRevision()!=0 ||
       SwissEphemeris::pathStatus()!=SwissEphemeris::PathStatus::Empty ||
       SwissEphemeris::calculate(1,2451545.,state) ||
       SwissEphemeris::calculateInterpolated(1,2451545.,state) || state.position[0]!=123. ||
       SwissEphemeris::removeFiles(files)!=files) return 1;
    std::cout<<"PASS: compiled-out adapter leaves state and files untouched\n";
}
