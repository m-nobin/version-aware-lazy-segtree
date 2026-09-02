# Registration record

Filled in once, at deposit time, after `bench/make_registration.sh` has written
`registration-manifest.txt` from a clean commit and that commit is tagged.
This file is outside the frozen file list on purpose: recording the deposit
must not change a frozen checksum. `bench/run_confirmatory.sh` refuses every
non-dry-run phase while the DOI line still reads Pending, and a real campaign
may run from any commit that descends from the registered one as long as
every frozen file still verifies against the manifest.

| Field | Value |
| --- | --- |
| Registered commit | Pending |
| Registered tag | Pending |
| Manifest SHA-256 | Pending |
| Immutable DOI / URL | Pending |
| Deposit UTC timestamp | Pending |
| Retrieval verified by, on | Pending |
