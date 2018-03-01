
[@deriving yojson]
type npmConcrete = (int, int, int, option(string));

[@deriving yojson]
type alpha = Alpha(string, option(num))
and num = Num(int, option(alpha));

[@deriving yojson]
type opamConcrete = alpha;

[@deriving yojson]
type opamRange = GenericVersion.range(opamConcrete);
[@deriving yojson]
type npmRange = GenericVersion.range(npmConcrete);

let viewNpmConcrete = ((m, i, p, r)) => {
  ([m, i, p] |> List.map(string_of_int) |> String.concat("."))
  ++
  switch r { | None => "" | Some(a) => a}
};

let rec viewOpamConcrete = (Alpha(a, na)) => {
  switch na {
  | None => a
  | Some(b) => a ++ viewNum(b)
  }
} and viewNum = (Num(a, na)) => {
  string_of_int(a) ++ switch na {
  | None => ""
  | Some(a) => viewOpamConcrete(a)
  }
};

module PendingSource = {
  [@deriving yojson]
  type t =
    /* url & checksum */
    | Archive(string, option(string))
    /* url & ref */
    | GitSource(string, option(string))
    | GithubSource(string, string, option(string))
    | NoSource;
};

/** Lock that down */
module Source = {
  [@deriving yojson]
  type t =
    /* url & checksum */
    | Archive(string, string)
    /* url & commit */
    | GitSource(string, string)
    | GithubSource(string, string, string)
    | NoSource;
};

[@deriving yojson]
type depSource =
  | Npm(GenericVersion.range(npmConcrete))
  | Github(string, string, option(string)) /* user, repo, ref (branch/tag/commit) */
  | Opam(GenericVersion.range(opamConcrete)) /* opam allows a bunch of weird stuff. for now I'm just doing semver */
  | Git(string)
  ;

let resolvedPrefix = "esyi3-";

[@deriving yojson]
type dep = (string, depSource);

let viewReq = req => switch req {
| Github(org, repo, ref) => "github: " ++ org ++ "/" ++ repo
| Git(s) => "git: " ++ s
| Npm(t) => "npm: " ++ GenericVersion.view(viewNpmConcrete, t)
| Opam(t) => "opam: " ++ GenericVersion.view(viewOpamConcrete, t)
};

type config = {
  esyOpamOverrides: string,
  opamRepository: string
};


let opamFromNpmConcrete = ((major, minor, patch, rest)) => {
  Alpha("",
    Some(
      Num(major, Some(Alpha(".", Some(
        Num(minor, Some(Alpha(".", Some(
          Num(patch, switch rest {
          | None => None
          | Some(rest) => Some(Alpha(rest, None))
          })
        ))))
      ))))
    )
  )
};
