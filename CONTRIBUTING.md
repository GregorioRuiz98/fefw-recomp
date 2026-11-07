# Contributing

Read [LEGAL.md](LEGAL.md), retain license headers and document the origin of third-party code. Contribute only material for which you have sufficient rights; existing software licenses do not cover game files.

Do not attach ROMs, keys, assets, commercial shaders, generated game C++, local executables or extracted fixtures. Public tests must use original synthetic data. Describe visual issues in words and remove personal paths, identifiers and credentials from shared messages.

Explain what changed, how you checked it and what remains unverified. Private tests with a local copy must not attach their data to the repository or GitHub Actions.

From the source folder prepared for GitHub, review the files being added and refresh the manifest:

```powershell
git add --all
python project/tools/audit_public_repo.py --repo . --write-manifest
git add source-manifest.json
python project/tools/audit_public_repo.py --repo . --history
```

The auditor checks the Git index, the manifest and, with `--history`, objects reachable from Git references. It does not replace provenance or license review. Removing a file from the latest commit does not remove copies or remote history.
