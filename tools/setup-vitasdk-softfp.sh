#!/usr/bin/env bash
# Install the softfp VitaSDK into ~/vitasdk and the packages the loader needs.
# Run inside WSL 2 / Linux (glibc >= 2.28). See docs/dev-setup.md.
#
# Why this isn't just `vdpm refresh nightly-softfp`: vdpm 0.1.4 only parses
# schema-1 channel manifests and the softfp manifest is schema 2. Its refresh
# script also hardcodes the hard-float `[vita]` repo. So we do what the refresh
# does by hand: verify the manifest's signature with vdpm's own tool and key,
# check both databases against the manifest's sha256s, and write pacman.conf.
set -euo pipefail

CHANNEL_BASE=https://vitasdk.org/channels
CHANNEL=nightly-softfp
HOST=x86_64-linux-gnu
PACKAGES=(vitaGL vitaShaRK SceShaccCgExt libmathneon kubridge zlib opensles taihen freetype)

export VITASDK=${VITASDK:-$HOME/vitasdk}
[[ ! -e $VITASDK ]] || { echo "$VITASDK already exists; remove it first" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

curl -fsSLO "$CHANNEL_BASE/$CHANNEL.json"
curl -fsSLO "$CHANNEL_BASE/$CHANNEL.json.sig"

field() { python3 -c "import json,sys; d=json.load(open('$CHANNEL.json')); print(eval(sys.argv[1]))" "$1"; }
core_release=$(field "d['core']['release']")
core_server=https://github.com/$(field "d['core']['repository']")/releases/download/$core_release
core_sha=$(field "d['core']['architectures']['$HOST']['database']['sha256']")
pkg_server=https://github.com/$(field "d['packages']['repository']")/releases/download/$(field "d['packages']['release']")
pkg_db=$(field "d['packages']['database']['name']")
pkg_sha=$(field "d['packages']['database']['sha256']")

echo ":: toolchain $core_release"
curl -fsSLO "$core_server/vitasdk-bootstrap-$HOST.tar.bz2"
curl -fsSLO "$core_server/vitasdk-bootstrap-$HOST.tar.bz2.sha256"
sha256sum -c "vitasdk-bootstrap-$HOST.tar.bz2.sha256"
tar xjf "vitasdk-bootstrap-$HOST.tar.bz2" -C "$(dirname "$VITASDK")"
grep -q 'float-abi=softfp' "$VITASDK/version_info.txt" || { echo "not a softfp toolchain" >&2; exit 1; }

# The trust anchor is the public key shipped in the checksummed bootstrap.
"$VITASDK/bin/vdpm-channel" verify "$CHANNEL.json" "$CHANNEL.json.sig" \
	"$VITASDK/share/vdpm/channel-public-key.pem"

curl -fsSL -o core.db "$core_server/$HOST.db"
curl -fsSL -o pkg.db "$pkg_server/$pkg_db"
[[ $(sha256sum core.db | cut -d' ' -f1) == "$core_sha" ]] || { echo "core db hash mismatch" >&2; exit 1; }
[[ $(sha256sum pkg.db | cut -d' ' -f1) == "$pkg_sha" ]] || { echo "package db hash mismatch" >&2; exit 1; }

repo=${pkg_db%.db}
mkdir -p "$VITASDK/var/lib/pacman/sync" "$VITASDK/var/lib/vdpm"
cp core.db "$VITASDK/var/lib/pacman/sync/$HOST.db"
cp pkg.db "$VITASDK/var/lib/pacman/sync/$repo.db"
cp "$CHANNEL.json" "$VITASDK/var/lib/vdpm/$CHANNEL.json"
cat > "$VITASDK/etc/pacman.conf" <<EOF
[options]
Architecture = $HOST vita_softfp
# Written by sf4-vita tools/setup-vitasdk-softfp.sh: vdpm 0.1.4 cannot parse the
# schema-2 $CHANNEL manifest. Signature and database sha256s were verified.
SigLevel = Never
[$HOST]
Server = $core_server
[$repo]
Server = $pkg_server
EOF

export PATH=$VITASDK/bin:$PATH VDPM_NONINTERACTIVE=1
vdpm install "${PACKAGES[@]}"

grep -q 'VITASDK=' ~/.bashrc || cat >> ~/.bashrc <<'RC'

# VitaSDK softfp (added for sf4-vita)
export VITASDK=$HOME/vitasdk
export PATH=$VITASDK/bin:$PATH
export CMAKE_GENERATOR=Ninja
RC

echo ":: done: $(head -2 "$VITASDK/version_info.txt" | tail -1)"
