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
| Registered commit | Pending |
| Registered tag | Pending |
| Manifest SHA-256 | Pending |
| Registration UTC timestamp (tag push) | Pending |
| Custody directory (outside every campaign tree) | Pending |
| Verified by, on | Pending |

The custody directory holds the blinding key, the label map and the input
manifests. It lives outside every campaign tree and outside the repository, so
the analyst half cannot reach it by path; the campaigns themselves are moved
into it before they are blinded. Record the absolute path above at registration
time.
