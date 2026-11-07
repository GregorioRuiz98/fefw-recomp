# Preparing a GitHub upload

Use only the source folder prepared for GitHub. The public ZIP is a separate artifact containing the standalone setup tool, corresponding source and licenses. The local package that runs the game must not be used as a release.

From the repository folder:

```powershell
python project/tools/audit_public_repo.py --repo . --history
git status --short
git log --oneline --all
```

If anything changes after review, update the manifest and repeat the checks described in `CONTRIBUTING.md`. `.gitignore` does not remove already tracked files.

For an empty GitHub repository, replace `OWNER/REPO` with the chosen destination:

```powershell
git remote add origin https://github.com/OWNER/REPO.git
git push -u origin codex/public-release
```

If the destination already contains work, integrate through a branch and review the diff. Do not force-push or store tokens in URLs or files.

Publish only the audited public ZIP containing the setup tool and corresponding source. Keep `LICENSE`, notices and build instructions alongside the binary. Do not attach `fefw.exe`, ExeFS/ROMFS, caches, saves, screenshots or game build outputs.

These checks do not constitute permission from rights holders or a legal opinion on the project in every jurisdiction.
