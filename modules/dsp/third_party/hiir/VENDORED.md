# HIIR (vendored subset)

- Library: HIIR 1.50 by Laurent de Soras, polyphase IIR half-band filters.
- Source: http://ldesoras.free.fr/src/hiir-1.50.zip (linked from http://ldesoras.free.fr/prod.html)
- Archive SHA-256: 8a13724e8d698a40f53687d40466443879f6250abd2229097f3990b76e6855a6
- Licence: WTFPL v2, see `license.txt` (copied verbatim from the archive).
- Files: only the scalar ("Fpu") 2x downsampler and upsampler and what they
  include, unmodified (`readme.txt` is the archive's own readme).

The half-band coefficients used by `modules/dsp/include/hba/dsp/oversampling.h` were generated once
with `hiir::PolyphaseIir2Designer::compute_coefs` from the same archive (not
vendored); the design targets are recorded next to the coefficient tables.
