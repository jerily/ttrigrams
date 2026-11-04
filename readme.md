# ttrigrams

Trigram substring search and typo suggestions for C and Tcl.

## Rules

- Index immutable byte strings; preserve case and punctuation.
- Union unique-trigram postings, then verify candidates against text substrings.
- Accept up to two Levenshtein edits. Suggestions require a shared trigram;
  queries shorter than three bytes use literal matching only.
- Callers own case folding, result ranking, and application policy.

## Tcl

```tcl
package require ttrigrams 0.1.0
set index [::ttrigrams::create descriptions {{web server} {encryption toolkit}}]
$index search encrytion
# {document 1 distance 1 shared_trigrams 5 query_trigrams 7}
$index search web 0   ;# literal matches only
$index destroy
```

`search query ?max_distance?` defaults to 2. Document IDs are zero-based input
positions. Results follow document order. Coverage is
`shared_trigrams / query_trigrams`; it is undefined for queries below three
bytes. Overlap alone never qualifies a result. Unicode distance counts UTF-8
bytes. Commands release their indexes on deletion or interpreter teardown.

## Build and C API

Requires C11, CMake 3.18+, and Tcl 8.6 or 9.x headers/stubs for the extension.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/prefix
```

Shared modules use Tcl stubs without linking the Tcl runtime. Set
`TCL_INCLUDE_PATH`, `TCL_STUB_LIBRARY`, and `TCL_TCLSH` for a custom Tcl install.
`BUILD_SHARED_LIBS=OFF` builds a wrapper for embedding; `TTRIGRAMS_BUILD_TCL=OFF`
builds only the static C library. Static embedding tests also need `TCL_LIBRARY`.

Use `find_package(ttrigrams 0.1 CONFIG REQUIRED)`, link `ttrigrams::core`, and
include `ttrigrams.h`. `ttrigrams_create` copies input texts; `ttrigrams_search`
visits matches; `ttrigrams_destroy` releases the index. Creation returns NULL
on invalid input/allocation failure; search returns -1, or propagates a nonzero
visitor result. Visitors must not destroy the index. Immutable indexes support
concurrent reads; the wrapper belongs to its Tcl interpreter.

Texts are limited to 4096 bytes and queries to 255 bytes. The Tcl wrapper
rejects embedded NUL. Corpus-size limits belong to the application.
