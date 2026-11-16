/* The build on screen (repro_marker, main.c): the commit it was built from
 * and, when psp-viewer had changes not committed, "+" and 4 hex of them
 * (tools/build_id.sh keeps them to rebuild that exact code). Rebuilt by
 * every make. */
const char build_id[]=BUILD_ID;
/* The game's version (VERSION at the root, tools/version.py): X.Y.Z-dev from
 * a local make, X.Y.Z-rc.N from a candidate (tools/release_candidate.py). */
const char game_version[]=GAME_VERSION;
