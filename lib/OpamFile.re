
open OpamParserTypes;
open OpamOverrides.Infix;

let expectSuccess = (msg, v) => if (v) { () } else { failwith(msg) };

type manifest = {
  fileName: string,
  build: list(list(string)),
  install: list(list(string)),
  /* TODO actually get the patches and files */
  patches: list(string), /* these should be absolute */
  files: list((string, string)), /* relname, sourcetext */
  deps: list(Types.dep),
  buildDeps: list(Types.dep),
  devDeps: list(Types.dep),
  peerDeps: list(Types.dep),
  optDependencies: list(Types.dep),
  /* TODO optDependencies (depopts) */
  source: option((string, option(string))),
  /* TODO add name_installed n stuff */
  /*
      "ocamlfind_version": {
        "val": "1.7.3--1",
        "scope": "global"
      },
      "ocamlfind_installed": {
        "val": "true",
        "scope": "global"
      },
      "ocamlfind_enable": {
        "val": "enable",
        "scope": "global"
      },
  */
  exportedEnv: list((string, (string, string))),
};
/* TODO parse an opam file into this manifest format */
/* Then parse our fancy override json or yaml thing... I think? */

type thinManifest = (string, string, string, VersionNumber.versionNumber);

let rec findVariable = (name, items) => switch items {
| [] => None
| [Variable(_, n, v), ..._] when n == name => Some(v)
| [_, ...rest] => findVariable(name, rest)
};

let opName = op => switch op {
  | `Leq => "<="
  | `Lt => "<"
  | `Neq => "!="
  | `Eq => "="
  | `Geq => ">="
  | `Gt => ">"
};

let fromPrefix = (op, version) => {
  let v = VersionNumber.versionNumberNumber(version);
  switch op {
  | `Eq => Semver.Exactly(v)
  | `Geq => AtLeast(v)
  | `Leq => AtMost(v)
  | `Lt => LessThan(v)
  | `Gt => GreaterThan(v)
  | `Neq => failwith("Unexpected prefix op for version " ++ opName(op) ++ " " ++ version)
  }
};

let withScope = name => "@opam/" ++ name;

let withoutScope = fullName => {
  let ln = 6;
  if (String.sub(fullName, 0, ln) != "@opam/") {
    failwith("Opam name not prefixed: " ++ fullName)
  };
  String.sub(fullName, ln, String.length(fullName) - ln);
};

let toDep = opamvalue => {
  let (name, s) = switch opamvalue {
  | String(_, name) => (name, Semver.Any)
  | Option(_, String(_, name), [Prefix_relop(_, op, String(_, version))]) => (name, fromPrefix(op, version))
  | Option(_, String(_, name), [y]) => {
    print_endline("Unexpected option " ++ name ++ " -- pretending its any " ++
    OpamPrinter.value(opamvalue));
    (name, Any)
  }
  | _ => {
    failwith("Can't parse this opam dep " ++ OpamPrinter.value(opamvalue))
  }
  };
  (withScope(name), s)
};

let processDeps = deps => {
  let deps = switch (deps) {
  | None => []
  | Some(List(_, items)) => items
  | Some(Group(_, items)) => items
  | _ => failwith("Can't handle the dependencies")
  };

  List.fold_left(
    ((deps, buildDeps, devDeps), dep) => {
      try (switch dep {
      /* This doesn't cover the case where there's an OR that has "test" on each side */
      | Option(_, value, [Ident(_, "build")]) => (deps, [toDep(value), ...buildDeps], devDeps)
      | Option(_, value, [Ident(_, "test")]) => (deps, buildDeps, [toDep(value), ...devDeps])
      | Option(_, String(_, name), [
        Logop(_, `And, Ident(_, "build"), Prefix_relop(_, op, String(_, version)))
      ]) => (deps, [(withScope(name), fromPrefix(op, version)), ...buildDeps], devDeps)
      | Option(_, String(_, name), [
        Logop(_, `And, Ident(_, "test"), Prefix_relop(_, op, String(_, version)))
      ]) => (deps, buildDeps, [(withScope(name), fromPrefix(op, version)), ...devDeps])
      | _ => ([toDep(dep), ...deps], buildDeps, devDeps);
      }) {
        | Failure(message) => {
          print_endline("Bailing on a dep " ++ message);
          (deps, buildDeps, devDeps)
        }
      }
    },
    ([], [], []),
    deps
  );
};

let filterMap = (fn, items) => {
  List.map(fn, items) |> List.filter(x => x != None) |> List.map(x => switch (x) { | Some(x) => x | None => assert(false)})
};

/** TODO handle more variables */
let variables = ((name, version)) => [
  ("jobs", "4"),
  ("make", "make"),
  ("ocaml-native", "true"),
  ("ocaml-native-dynlink", "true"),
  ("bin", "$cur__install/bin"),
  ("lib", "$cur__install/lib"),
  ("man", "$cur__install/man"),
  ("pinned", "false"),
  ("name", name),
  ("prefix", "$cur__install"),
];

let replaceVariables = (info, string) => {
  let string = string
    |> Str.global_replace(Str.regexp("%{\\(.+\\):installed}%"), "${\\1_installed:-false}")
    |> Str.global_replace(Str.regexp("%{\\(.+\\):enable}%"), "${\\1_enable:-disable}")
    |> Str.global_replace(Str.regexp("%{\\(.+\\):version}%"), "${\\1_version}")
    |> Str.global_replace(Str.regexp("%{\\(.+\\):bin}%"), "${\\1.bin}")
    |> Str.global_replace(Str.regexp("%{\\(.+\\):share}%"), "${\\1.share}")
    |> Str.global_replace(Str.regexp("%{\\(.+\\):lib}%"), "${\\1.lib}")
  ;
  List.fold_left(
    (string, (name, res)) => {
      Str.global_replace(
        Str.regexp_string("%{" ++ name ++ "}%"),
        res,
        string
      )
    },
    string,
    variables(info)
  )
};

let processCommand = (info, items) => {
  items |> filterMap(item => {
    switch item {
    | String(_, name) => Some(replaceVariables(info, name))
    | Ident(_, ident) => {
      switch (List.assoc_opt(ident, variables(info))) {
      | Some(string) => Some(string)
      | None => {
        print_endline("⚠️ Missing vbl " ++ ident);
        None
      }
      }
    };
    | _ => {
      print_endline("Bad build arg " ++ OpamPrinter.value(item));
      None
    }
    };
  })
};

/** TODO handle optional build things */
let processCommandList = (info, item) => {
  switch(item) {
  | None => []
  | Some(List(_, items))
  | Some(Group(_, items)) => {
    switch items {
    | [String(_) | Ident(_), ...rest] => {
      [items |> processCommand(info)]
    }

    | _ =>
    items |> filterMap(item => {
      switch item {
      | List(_, items) => {
        Some(processCommand(info, items))
      }
      | Option(_, List(_, items), _) => {
        Some(processCommand(info, items))
      }
      | _ => {
        print_endline("Skipping a non-list build thing " ++ OpamPrinter.value(item));
        None
      }
      }
    });
    }

  }

  | Some(Ident(_, ident)) => {
    switch (List.assoc_opt(ident, variables(info))) {
    | Some(string) => [[string]]
    | None => {
      print_endline("⚠️ Missing vbl " ++ ident);
      []
    }
    }

  }
  | Some(item) => failwith("Unexpected type for a command list: " ++ OpamPrinter.value(item))
  };
};

let processStringList = item => {
  let items = switch(item) {
  | None => []
  | Some(List(_, items))
  | Some(Group(_, items)) => items
  | Some(item) => failwith("Unexpected type for a string list: " ++ OpamPrinter.value(item))
  };
  items |> filterMap(item => {
    switch item {
      | String(_, name) => Some(name)
      | _ => {
        print_endline("Bad string list item arg " ++ OpamPrinter.value(item));
        None
      }
    }
  });
};

let findArchive = contents => {
  switch (findVariable("archive", contents)) {
  | Some(String(_, archive)) => archive
  | _ => {
    switch (findVariable("http", contents)) {
    | Some(String(_, archive)) => archive
    | _ =>
    switch (findVariable("src", contents)) {
    | Some(String(_, archive)) => archive
    | _ => failwith("Invalid url file - no archive")
    }
  }
  }
  }
};

let parseUrlFile = ({file_contents}) => {
  let archive = findArchive(file_contents);
  switch (findVariable("checksum", file_contents)) {
  | Some(String(_, checksum)) => Some((archive, Some(checksum)))
  | _ => Some((archive, None))
  }
};

let toDepSource = ((name, semver)) => (name, Types.Opam(semver));

let getOpamFiles = (opam_name) => {
  let dir = Filename.concat(Filename.dirname(opam_name), "files");
  if (Files.isDirectory(dir)) {
    let collected = ref([]);
    Files.crawl(dir, (rel, full) => {
      collected := [(rel, Files.readFile(full) |! "opam file unreadable"), ...collected^]
    });
    collected^;
  } else {
    []
  }
};

let parseManifest = (info, {file_contents, file_name}) => {
  let baseDir = Filename.dirname(file_name);
  let (deps, buildDeps, devDeps) = processDeps(findVariable("depends", file_contents));
  let (depopts, _, _) = processDeps(findVariable("depopts", file_contents));
  let files = getOpamFiles(file_name);
  let patches = processStringList(findVariable("patches", file_contents));
  /** OPTIMIZE: only read the files when generating the lockfile */
  /* print_endline("Patches for " ++ file_name ++ " " ++ string_of_int(List.length(patches))); */
  {
    fileName: file_name,
    build: processCommandList(info, findVariable("build", file_contents)) @ [
      ["sh", "-c", "(esy-installer || true)"]
    ],
    install: processCommandList(info, findVariable("install", file_contents)),
    patches,
    files,
    deps: (deps |> List.map(toDepSource)) @ [
      /* HACK? Not sure where/when this should be specified */
      ("@esy-ocaml/substs", Npm(Semver.Any)),
      ("@esy-ocaml/esy-installer", Npm(Semver.Any))
    ],
    buildDeps: buildDeps |> List.map(toDepSource),
    devDeps: devDeps |> List.map(toDepSource),
    peerDeps: [], /* TODO peer deps */
    optDependencies: depopts |> List.map(toDepSource),
    source: None,
    exportedEnv: [],
  };
};

let parseDepVersion = ((name, version)) => {
  PackageJson.parseNpmSource((name, version))
};

module StrSet = Set.Make(String);
let assignAssoc = (target, override) => {
  let replacing = List.fold_left(
    ((set, (name, _)) => StrSet.add(name, set)),
    StrSet.empty,
    override
  );
  List.filter(((name, _)) => !StrSet.mem(name, replacing), target) @ override
};

module O = OpamOverrides;
let mergeOverride = (manifest, override) => {
  let source = override.O.opam |?> (opam => switch opam.O.url {
    | Some(url) => Some((url, opam.O.checksum))
    | None => None
    }) |?? manifest.source;
     /* |? manifest.source; */
  {
    ...manifest,
    build: override.O.build |? manifest.build,
    install: override.O.install |? manifest.install,
    deps: assignAssoc(manifest.deps, override.O.dependencies |> List.map(parseDepVersion)),
    peerDeps: assignAssoc(manifest.peerDeps, override.O.peerDependencies |> List.map(parseDepVersion)),
    files: manifest.files @ (override.O.opam |?>> (o => o.O.files) |? []),
    source: source,
    exportedEnv: override.O.exportedEnv
  }
};

let getManifest = (opamOverrides, (opam, url, name, version)) => {
  let manifest = {
    ...parseManifest((name, version), OpamParser.file(opam)),
    source: Files.exists(url) ? parseUrlFile(OpamParser.file(url)) : None
  };
  switch (OpamOverrides.findApplicableOverride(opamOverrides, name, version)) {
  | None => {
    /* print_endline("No override for " ++ name ++ " " ++ VersionNumber.viewVersionNumber(version)); */
    manifest
  }
  | Some(override) => {
    /* print_endline("!! Found override for " ++ name); */
    let m = mergeOverride(manifest, override);
    m
  }
  }
};

let getArchive = ({source}) => source;

let process = ({deps, buildDeps, devDeps}) => {
  (deps, buildDeps, devDeps)
};

let commandListToJson = e => e |> List.map(items => `List(List.map(item => `String(item), items)));

let toPackageJson = (opamOverrides, filename, name, version) => {
  let manifest = getManifest(opamOverrides, (filename, "", withoutScope(name), switch version {
  | `Opam(t) => t
  | _ => failwith("unexpected opam version")
  }));

  (`Assoc([
    ("name", `String(name)),
    ("version", `String(Lockfile.plainVersionNumber(version))),
    ("esy", `Assoc([
      ("build", `List(commandListToJson(manifest.build))),
      ("install", `List(commandListToJson(manifest.install))),
      ("buildsInSource", `Bool(true)),
      ("exportedEnv", `Assoc(
        ([
          (withoutScope(name) ++ "_version", (Lockfile.plainVersionNumber(version), "global")),
          (withoutScope(name) ++ "_installed", ("true", "global")),
          (withoutScope(name) ++ "_enable", ("enable", "global")),
        ] @
        manifest.exportedEnv)
        |> List.map(((name, (val_, scope))) => (
          name,
          `Assoc([
            ("val", `String(val_)),
            ("scope", `String(scope))
          ])
        ))
      ))
      /* ("buildsInSource", "_build") */
    ])),
    ("_resolved", `String(Types.resolvedPrefix ++ name ++ "--" ++ Lockfile.viewRealVersion(version))),
    ("peerDependencies", `Assoc([
      ("ocaml", `String("*")) /* HACK probably get this somewhere */
    ])),
    ("optDependencies", `Assoc(
      (manifest.optDependencies |> List.map(((name, _)) => (name, `String("*"))))
    )),
    ("dependencies", `Assoc(
      (manifest.deps |> List.map(((name, _)) => (name, `String("*"))))
      @
      (manifest.buildDeps |> List.map(((name, _)) => (name, `String("*"))))
    ))
  ]), manifest.files, manifest.patches)
};

/* let process = (parsed: OpamParserTypes.opamfile) => switch parsed {
  | { file_contents, file_name } => switch file_contents {
  | [] => failwith((""))
  | [item, ..._] => switch item {
  | OpamParserTypes.Section(_, _) => failwith((""))
  | OpamParserTypes.Variable(a, b, c) => switch c {
  | OpamParserTypes.Bool(_, _) => failwith ("")
  | OpamParserTypes.Int(_, _) => failwith ("")
  | OpamParserTypes.String(_, _) => failwith ("")
  | OpamParserTypes.Relop(_, _, _, _) => failwith ("")
  | OpamParserTypes.Prefix_relop(_, r, _) => switch r {
  | `Leq => failwith ("")
  | `Lt => failwith ("")
  | `Neq => failwith ("")
  | `Eq => failwith ("")
  | `Geq => failwith ("")
  | `Gt => failwith ("")
  }
  | OpamParserTypes.Logop(_, l, _, _) => failwith("")
  | OpamParserTypes.Pfxop(_, p, _) => failwith ("")
  | OpamParserTypes.Ident(_, _) => failwith ("")
  | OpamParserTypes.List(_, _) => failwith ("")
  | OpamParserTypes.Group(_, _) => failwith ("")
  | OpamParserTypes.Option(_, _, _) => failwith ("")
  | OpamParserTypes.Env_binding(_, _, _, _) => failwith ("")
  }
  }
  }
  }; */
