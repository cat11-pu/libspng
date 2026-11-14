# Decoder audit tools

- `gen_corpus.py`: deterministic generator for 500 valid and 3000 malformed PNGs.
- `png_audit.c`: libspng/libpng differential harness with per-call chunk/offset
  failure reporting.

The final report and exact reproduction commands are in
`artifacts/audit-3592/REPORT.md`.
