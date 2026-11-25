<!-- SPDX-License-Identifier: MIT -->
# Rhythmer screens

A plug-in draws its own page on the MPC Sample's screen in Rhythmer, with its
own code. Everything here is MIT licensed (LICENSE): copy it into a plug-in.

- `rhythmer_screen.h`: the format, plain C, for CLAP and VST2 plug-ins.
- `rhythmer_companion.h`: a screen installed apart from the plug-in, for a
  plug-in as it is released ([docs/PLUGIN_COMPANIONS.md](../../docs/PLUGIN_COMPANIONS.md)).
- `example/`: Screen Example, a drive effect that draws its page and its
  strip; one source, one library with both entry points.
- The reference: [docs/PLUGIN_SCREENS.md](../../docs/PLUGIN_SCREENS.md).

Build the example for the MPC Sample (aarch64 Linux) and for 32-bit ARM:

```sh
cd example
make CLAP_INCLUDE=/path/to/clap/include
make armv7
```

Copy `screen-example.clap` or `screen-example.so` into `Rhythmer/Plugins` on
the card. To see the screen without an MPC, Rhythmer's own binary writes it as
PNGs after each step:

```sh
rhythmer --rhythmer-plugin-screen screen-example.clap out wheel:10 plus push k1:64
```
