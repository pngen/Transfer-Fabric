# Integrity

Optional integrity modes: `none`, `crc32c`, `sha256`.

- Whole-transfer and per-chunk verification.
- For memory/device transfers the destination content is hashed and compared to the source hash.
- A mismatch is a hard `integrity_failure`; success is never reported after a failed verification.
- CRC32C and SHA256 are self-contained implementations validated against known vectors.
