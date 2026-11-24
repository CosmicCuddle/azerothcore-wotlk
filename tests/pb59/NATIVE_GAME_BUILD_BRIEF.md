# Core88 native Game build brief

Owner: CosmicCuddle. Assignment: coordination #81 comment7146046931.
Resource request to the existing native build tester: #52 comment7146136903.
This is a development build gate, not a module/production acceptance repin.

## Exact inputs and what remains unknown

| Input | Pinned or required evidence |
| --- | --- |
| Core under test | CosmicCuddle/azerothcore-wotlk `d5eee59878bed7991611b0bf6764e73e03885b39` (Core88) |
| Source branch | fix/codex-pb59-recovery-save-contract-20261124 |
| Parent | Core85 `72efa4a5c74e9ad0636f08e561e7a00770db849b` |
| Baseline Core | Core74 `c779e7b4f735cab048f5b351ca6b9053f188787a` |
| Baseline Playerbots | PB56 `66c66ddc3dc07ea81b5f27d96fec4f77f907afe3` |
| Playerbots candidate | PB57 `4e1f253e` is separately tested; not permission to substitute it for the frozen baseline |
| Other modules | Not established by this checkout; tester/CS01 must supply every module repo/full SHA/clean state |
| Linkage/defines | Actual enabled/disabled modules, linkage, feature defines and build configuration required |
| Dev resource | Existing owner-approved disposable source/build roots and approval scope/reference required |

The inspected Windows Core checkout has no module source directories, .gitmodules or module
gitlinks, and no configured build/compile_commands.json. A clean Core status therefore cannot
establish a module manifest. Ignored module directories in another checkout still need their
own repository/SHA/cleanliness inventory. Never infer installed/live versions from these pins.

The existing local GCC16.2 MinGW compiler runs standalone C++20 headers/fixtures. It is not an
approved configured Game environment. The earlier real-header probe stopped at missing Boost;
no dependency probe, package installation, configure or native compilation is repeated here.
The tester's owner-approved GRM venv/browser environment does not itself establish a Core build
or approval for C++ dependency installation. Remote development resource availability is unknown
until the tester answers; this is not a claim that no suitable environment exists elsewhere.

## Build-system requirements, from the exact Core source

- Root CMakeLists.txt requires CMake3.16 and exports compile_commands.json.
- src/cmake/macros/ConfigureBaseTargets.cmake selects C++20. Legacy compiler version floors in
  platform settings alone do not establish support for the source's C++20/library features.
- deps/boost/CMakeLists.txt requests Boost>=1.78 on Windows or>=1.74 otherwise, with filesystem,
  program_options, iostreams and regex; thread is added for configured Worldserver modules.
- Root CMake and deps/mysql require detected MySQL client headers/library; deps/openssl requires
  Crypto/SSL. Record the actual selected versions/paths, not assumed platform defaults.
- Bundled dependencies, generated revision/settings headers, compiler/generator/configuration,
  architecture, PCH and module hooks must match that configured source. No fixture includes,
  type-only extracts, disabled checks or fabricated dependency headers.

## Minimal compile gate, after resource/pins are confirmed

Use the approved disposable source at the exact Core88 SHA with a clean tree and explicit
module manifest. This documentation/test child does not replace that requested build SHA.
Confirm the generated build graph includes PlayerPB59Recovery.cpp and current Item/Player/Bag
headers; a successful stale game archive omitting the collector is not this gate.

Read the actual object target/command from its generated graph/compile_commands, preserving
generated headers, includes, defines and PCH dependencies. Do not guess an object target or
construct new ad-hoc compiler flags. Then use the existing configured build:

```text
cmake --build <approved-build> --target <actual-generated-collector-object-target> --parallel 2
cmake --build <approved-build> --target game --parallel 2
```

Placeholders are not executable instructions until the tester confirms those paths/targets.
If the generator exposes no individual object target, report that and use the real game target.
For multi-configuration generators record/preserve the actual --config selection. These are
compile-only development commands, not configure, install, server launch or deployment commands.

Report source/module pins and cleanliness, compiler/CMake/generator/dependency versions,
sanitized invocation, exit status, first diagnostics and log identifier. Do not post credentials,
full environment/CMake cache, DB settings or production paths. If a dependency/resource is missing,
stop with that precise requirement and the owner authorization needed; no unapproved installation.

A module-disabled Core Game compile is a separate, limited result. Module compatibility,
Worldserver linking and actual gameplay/DB recovery need their own subsequent evidence. No CI,
production access, activation, merge, repin, live config/schema/SQL/DBC/client or binary changes.
