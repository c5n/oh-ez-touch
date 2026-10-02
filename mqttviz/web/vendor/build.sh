#!/bin/sh
# Rebuild three.bundle.js: three.js and the few addons the space view
# uses, as one minified ES module -- one file to serve, no import map,
# nothing fetched by the browser from anywhere but this server.
#
#   mqttviz/web/vendor/build.sh [version]
#
# Needs npm (for `npm pack` and `npx esbuild`) on the machine that
# rebuilds it, never on the one that runs mqttviz.
set -eu
VERSION=${1:-0.186.1}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cd "$WORK"
npm pack "three@$VERSION" >/dev/null
mkdir -p node_modules/three
tar xzf "three-$VERSION.tgz" -C node_modules/three --strip-components=1
cp "$HERE/entry.js" .
npx -y esbuild@0.25 entry.js --bundle --format=esm --minify \
    --legal-comments=inline --outfile="$HERE/three.bundle.js"
cp node_modules/three/LICENSE "$HERE/LICENSE.three"
echo "three.js $VERSION bundled"
