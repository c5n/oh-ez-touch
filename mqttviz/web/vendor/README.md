# Vendored for the space view

`three.bundle.js` is three.js **0.186.1** (MIT, see `LICENSE.three`) plus
the addons named in `entry.js` (OrbitControls, CSS2DRenderer and the
bloom post-processing chain), bundled into one minified ES module by
`build.sh`. It is checked in so mqttviz keeps its rule of nothing but
Python 3 on the machine that runs it, and so the page works on a LAN
without internet access.
