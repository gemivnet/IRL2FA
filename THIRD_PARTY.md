# Third-party components

IRL2FA bundles or derives from the following third-party works. Each keeps its
own licence; the IRL2FA licences (see [README](README.md#license)) cover only
the original work in this repository.

| Component | Where | Licence | Source |
|-|-|-|-|
| Monocypher (X25519, BLAKE2b, ChaCha20) | `firmware/core/third_party/monocypher/` | CC0-1.0 or BSD-2-Clause (dual) | https://monocypher.org/ |
| BIP-39 English wordlist | `firmware/core/wordlist.c` (generated) | MIT | https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt |
| KiCad symbol / footprint / 3D libraries (parts copied into `hardware/lib/`) | `hardware/lib/` | CC-BY-SA-4.0 with the KiCad libraries exception | https://www.kicad.org/libraries/license/ |

The KiCad libraries exception means using these symbols and footprints in a
design does not by itself put the design under CC-BY-SA; the copied library
files themselves remain CC-BY-SA-4.0.
