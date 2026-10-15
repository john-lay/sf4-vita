#!/usr/bin/env bash
# Build a VitaSDK CMake project from Windows without leaving the shell:
#   MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-24.04 -e bash /mnt/d/Repo/sf4-vita/tools/wsl-build.sh /mnt/d/path/to/project
# Copies the project into /tmp (faster than building on /mnt), builds it, prints
# only undefined references and errors, and copies the resulting *.vpk back.
export VITASDK=${VITASDK:-$HOME/vitasdk} CMAKE_GENERATOR=Ninja
export PATH=$VITASDK/bin:$PATH

src=${1:?usage: wsl-build.sh <project dir>}
work=/tmp/$(basename "$src")
rm -rf "$work" && cp -r "$src" "$work" && cd "$work" || exit 1

cmake -S . -B build > build.log 2>&1 && cmake --build build >> build.log 2>&1
rc=$?
grep -oE "undefined reference to .[^']+'" build.log | sort | uniq -c | sort -rn | head -30
grep -E ' error|Error ' build.log | grep -v 'undefined reference' | head -15
if [[ $rc -eq 0 ]]; then
	ls -l build/*.vpk
	cp build/*.vpk "$src/"
else
	echo "BUILD FAILED (full log: $work/build.log)"
fi
exit $rc
