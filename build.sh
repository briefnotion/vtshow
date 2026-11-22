#!/bin/sh
# Build the scene programs and the show. Run it from anywhere.
# The scenes in ports/ are optional: if that folder is not there, only the
# bonfire, moonphases, starburst, singularity and thunderstorm scenes and a show with
# those five are built.
set -e
cd "$(dirname "$0")"
FLAGS="-O2 -std=c++17"
SCENES="bonfire/bonfire.cpp moonphases/moonphases.cpp starburst/starburst.cpp singularity/singularity.cpp thunderstorm/thunderstorm.cpp"
SHOWFLAGS="-DVTSHOW_NO_MAIN"
if [ -d ports ]; then
    for d in campfire moon fireworks blackhole; do
        echo "building ports/$d ..."
        g++ $FLAGS -o "ports/$d/$d" "ports/$d/$d.cpp"
    done
    SCENES="ports/campfire/campfire.cpp ports/moon/moon.cpp ports/fireworks/fireworks.cpp ports/blackhole/blackhole.cpp $SCENES"
    SHOWFLAGS="$SHOWFLAGS -DVTSHOW_PORTS"
fi
for d in bonfire moonphases starburst singularity thunderstorm; do
    echo "building $d ..."
    g++ $FLAGS -o "$d/$d" "$d/$d.cpp"
done
echo "building show ..."
g++ $FLAGS $SHOWFLAGS -o show/show show/show.cpp $SCENES
echo "done. try:  ./show/show   or   ./bonfire/bonfire --loop   (q quits, --no-color for the plain console)"
