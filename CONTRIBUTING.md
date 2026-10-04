# Contributing

Keep protocol behavior separate from drivers and application policy. Portable
headers must compile without Arduino, ESP-IDF, or platform header shims. New hardware
or network integrations belong in adapters, not conditional branches in the parser.

Run CMake/CTest and the Arduino example matrix. Add a deterministic host regression
for every parser, timer, wire-format, or recovery change. Preserve bounded work,
packet ownership rules, and explicit failure results. Hardware verification must
be reported separately; a successful compile does not prove timing or interoperability.

Retain upstream copyright notices. Contributions to this GPL-2.0-only repository
must be license-compatible. Do not commit credentials, private uNode application
code, reference PDFs, ESTA standards, or hardware-specific personal configuration.
