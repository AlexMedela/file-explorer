# Maintenance

## Releasing a new version

The version in `src/app.c` (`APP_VERSION`) is the source for the package version files. Once the new version number is known, complete these steps **before committing**:

1. Set `APP_VERSION` in `src/app.c` to the new version, without the `v` prefix.
2. Move the release notes from `## Unreleased` in `CHANGELOG.md` into a `## <version>` section. Leave an `## Unreleased` section for future changes.
3. Run `sh tools/sync-package-versions.sh`. It updates the RPM spec, Debian `.dsc`, and Debian changelog from `APP_VERSION`.
4. Run `sh tools/check-release-version.sh v<version>`. Review the package changes and commit them with the app version and release notes.

For example, for version `0.11.3`:

```sh
# After editing src/app.c and CHANGELOG.md:
sh tools/sync-package-versions.sh
sh tools/check-release-version.sh v0.11.3
git diff --check
git diff -- src/app.c CHANGELOG.md packaging/obs
```

CI checks the versions on pushes and pull requests. The release workflow checks that the pushed tag matches them before it builds. After the release commit is on the release branch, create and push its `v<version>` tag; that starts the GitHub release workflow. Do not tag an earlier commit that lacks the package updates.

## OBS setup

OBS keeps its own copy of `packaging/obs/_service`. Update that hosted copy **once** to use `revision @PARENT_TAG@`; changing the file in Git does not change the hosted copy. After that, the release workflow's runservice trigger fetches the tagged source, and OBS `set_version` applies the tag's version to its build descriptions. The tag must already be available on GitHub when the trigger runs.

## Packaging changes without a new app version

For a rebuild of the same version, increase the RPM spec's `Release` value or the Debian packaging revision in both `file-explorer.dsc` and the first `debian.changelog` entry. The sync command preserves an existing Debian revision for the current app version. Run the version check before committing.
