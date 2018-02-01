
/**
 * Once I've solved all the other things, figure out what build deps I have, and try to cull them down
 * to deduplicate things that fit within the same semver range.
 * Then get them, and resolve deps as though they were top-level, but *sharing the same build_modules cache*.
 * This way build_modules live in a single place.
 *
 *
 * build_modules/ <-- applies to whole tree!
 *    some-tool.1.1.0/
 *      src/
 *      esy_modules/
 *    other-tool.3.4.0/
 * esy_modules/
 *    something
 *      src/
 *    otherdep
 *      src/
 *    third
 *      src/
 * dev_modules/
 *    something
 *      src/
 *    otherdep
 *      src/
 *
 *
 * // for ditry checking
 * requestedDeps: {[key: string]: string},
 * requestedBuildDeps: {[key: string]: string},
 *
 * solvedDeps: {
 *   "name": {
 *     version: "concrete-version",
 *     location: "/path"
 *     source: "url"
 *     hash: "somehash"
 *     solvedBuildDeps: {
 *       "name": "concrete-version"
 *     }
 *   }
 * }
 * solvedBuildDeps: {
 *  "name": "concrete-version"
 * }
 * allBuildDeps: {
 *  "name": {
 *    "concrete-version": {
 *      solvedDeps: {
 *      }
 *      solvedBuildDeps: {
 *        "name": "concrete-version"
 *      }
 *    }
 *  }
 * }
 *
 */

type solvedDep = {
  name: string,
  version: string,
  archive: string, /* git+some.git or some.zip or some.tgz */
  checksum: string,
  unpackedLocation: string,
  buildDeps: list((string, string)),
}

and lockfile = {
  requestedDeps: list(Types.dep),
  requestedBuildDeps: list(Types.dep),
  /* TODO dev deps, they need to be split into devBuildDeps probably */
  /* specifiedDevDeps: list(Types.dep), */
  /* solvedDevDeps: list(solvedDep), */

  solvedDeps: list(solvedDep),
  solvedBuildDeps: list((string, string)),
  /* A mapping of name:version to the solved dependencies, and the solved build deps */
  allBuildDeps: list(((string, string), list(solvedDep), list((string, string)))),
};

let empty = {
  requestedDeps: [],
  /* specifiedDevDeps: [], */
  requestedBuildDeps: [],

  solvedDeps: [],
  solvedBuildDeps: [],
  /* solvedDevDeps: [], */

  allBuildDeps: [],
};