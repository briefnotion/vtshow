#!/bin/sh
# Build every scene as its own program, and the show that plays them all in turn.
# Run it from anywhere. To add a scene: put it in a folder of its own like the
# others, list it in SCENES below, declare its make_<name>() in scenes.h and add a
# push_back line for it in show/show.cpp.
set -e
cd "$(dirname "$0")"

if [ ! -f vtscreen/vtscreen.h ]; then
    echo "vtscreen/vtscreen.h is missing: the library is a git submodule. Run:" >&2
    echo "    git submodule update --init" >&2
    exit 1
fi

SCENES="bonfire moonphases starburst singularity thunderstorm meadow matrix"
FLAGS="-O2 -std=c++17"
case "$(uname -s)" in
    MINGW*) FLAGS="$FLAGS -static" ;;    # MSYS2 MinGW-w64 on Windows: no DLLs needed to run
esac

for d in $SCENES; do
    echo "building $d ..."
    g++ $FLAGS -o "$d/$d" "$d/$d.cpp"
done

echo "building show ..."
FILES=""
for d in $SCENES; do FILES="$FILES $d/$d.cpp"; done
g++ $FLAGS -DVTSHOW_NO_MAIN -o show/show show/show.cpp $FILES

echo "done. try:  ./show/show   or   ./bonfire/bonfire --loop   (q quits, --no-color for the plain console)"
