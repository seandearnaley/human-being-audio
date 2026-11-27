# Release a studio product

1. Merge the product changes with its new three-part version in the product’s top-level CMakeLists.txt. This version becomes the plug-in version, package filenames and product-scoped release tag.
Add its musician-facing changes in `products/<product>/docs/releases/<version>.md`. The workflow publishes that file with the source link and verification instructions.

2. To qualify a private release candidate, in GitHub Actions open **Studio release**, select **Run workflow**, keep the branch on **main** and choose the product. From any authenticated terminal, the equivalent command is `gh workflow run release.yml -R seandearnaley/human-being-audio --ref main -f product=livecut-redux`. Use the catalogue slug for the product you are shipping.
3. GitHub builds and tests all three platforms, signs and installs their packages, checks the installed VST3 publisher and host class IDs, opens the plug-ins in pluginval, and verifies the Windows uninstaller. macOS also validates the Audio Unit.
4. When all three packages pass, the workflow verifies provenance and attaches the catalogue record and checksums to the draft. Maintainers can download the exact signed files with `gh release download <product>/v<version>`. The draft is visible to repository writers. After explicit release approval, publish the verified draft with `gh release edit <product>/v<version> --draft=false --latest=false`, then run **Verify live downloads** for that tag. To build and publish in one previously approved run, pass `-f publish=true` when dispatching the workflow.

5. After publication, **Verify live downloads** opens the production site in Chromium and waits for the product card to show the published version. It clicks the macOS, Windows and Ubuntu links, checks the downloaded bytes against the attested release manifest, and runs `gh release verify-asset` on each file. The job log records the filename, size and verified SHA-256 for each platform.

A red download check means the release is already published. After fixing the site, rerun the failed job or dispatch **Verify live downloads** for the tag currently served by the site. This dispatch verifies the published files using read access to the release and its attestations.

```sh
gh workflow run verify-downloads.yml --ref main -f tag=pole-field/v0.1.0
```

Each release contains a universal macOS PKG, a Windows x64 installer, an Ubuntu 26.04 x86_64 archive, SHA256SUMS and release.json. Verify a downloaded file with `gh attestation verify FILE -R seandearnaley/human-being-audio`. Clone the source at the commit recorded in release.json as described in the repository README.

## Investigating a slow or failed qualification

Report the product, platform, failed step and run link when the error appears. For a DAW crash, capture the host PID and its crash record, reproduce the smallest failing gesture with one plugin, and run that gesture against the fix before resuming family checks. A publication hold leaves diagnosis and repair active.

Start desktop validation as each platform asset arrives. Record the source commit and package SHA-256 alongside each result. A retry can reuse completed checks for those exact bytes. For a slow workflow, separate time in the runner queue from compile, CTest, notarization and installed-package checks using the Actions job timestamps.

## Signing credentials and access

Contributors need only the development toolchain. Signing runs on disposable GitHub-hosted machines. The **release** environment accepts only the main branch. Maintainers with workflow access can dispatch a release from any machine.

| Platform | What the workflow uses | What maintainers renew |
| --- | --- | --- |
| Apple | Developer ID Application signs the VST3, AU and app. Developer ID Installer signs the PKG. App Store Connect submits the PKG for notarization, then the workflow staples and checks the ticket. | Replace the two base64 PKCS#12 exports, their shared export password and identity names when the certificates change. Replace the notary key, key ID and issuer ID if rotated. |
| Windows | GitHub OIDC authenticates to Azure Artifact Signing. Its signer role covers the selected Public Trust profile. Microsoft timestamps the standalone EXE, the VST3 binary, the uninstaller and the installer. NSIS builds the installer. | Maintain the Azure identity validation and certificate profile. The environment contains Azure resource identifiers. Microsoft rotates the short-lived signing certificates. |
| Ubuntu | GitHub signs provenance for the final archive and the other release assets. | GitHub issues the workflow identity for each run. |

To set the Apple export password, run `gh secret set MACOS_P12_PASSWORD --env release -R seandearnaley/human-being-audio` and enter it at the hidden prompt. The other Apple secret names are in the signing step of the workflow.

A packaging failure prevents publication. Open the failed step in Actions. After correcting an external credential, use **Re-run failed jobs**. A source fix needs a fresh run from the corrected commit. A draft for the same source commit can be resumed. An existing tag or a draft targeting different source prevents another run for that version.

[Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution) · [Microsoft Artifact Signing](https://github.com/Azure/artifact-signing-action) · [NSIS licensing](https://nsis.sourceforge.io/License) · [GitHub artifact attestations](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations)
