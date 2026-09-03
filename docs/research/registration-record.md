# Registration record

Filled in once, at registration time, after `bench/make_registration.sh` has
written `registration-manifest.txt` from a clean commit and that commit is
tagged and pushed. This file is outside the frozen file list on purpose:
recording the registration must not change a frozen checksum.
`bench/run_confirmatory.sh` refuses every non-dry-run phase while the
registered tag still reads Pending, and a real campaign may run from any
commit that descends from the registered one as long as every frozen file
still verifies against the manifest. The protocol and manifest are published
with the paper's arXiv submission.

| Field | Value |
| --- | --- |
| Registered commit | `d462dcd9d3bf1a10aae372a4649388c9fa09c46a` (the manifest names its parent `baa1b4e20f73040dce99a4bde4cdb76e80ae9bd0`, the commit its checksums were taken from; the two differ only by the manifest file itself, which is not frozen) |
| Registered tag | `registered-20260903` |
| Manifest SHA-256 | `f253a6f2a3c612efa1c7cf4720b99ce6364eb2d95fcab5232d36b216c2ba4493` |
| Registration UTC timestamp (tag push) | 2026-09-03T07:47:14Z |
| Custody directory (outside every campaign tree) | `/Users/nobin/valseg-custody-confirmatory` |
| Verified by, on | Mohammad Nobinur, 2026-09-03 |

The custody directory holds the blinding key, the label map and the input
manifests. It lives outside every campaign tree and outside the repository, so
the analyst half cannot reach it by path; the campaigns themselves are moved
into it before they are blinded. Record the absolute path above at registration
time.
