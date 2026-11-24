# Release a studio product

1. Merge the product changes with its new three-part version in the product’s top-level CMakeLists.txt. This version becomes the plug-in version, package filenames and product-scoped release tag.
Add its musician-facing changes in `products/<product>/docs/releases/<version>.md`. The workflow publishes that file with the source link and verification instructions.

2. In GitHub Actions, open **Studio release**, select **Run workflow**, keep the branch on **main** and choose the product. From any authenticated terminal, the equivalent command is `gh workflow run release.yml -R seandearnaley/human-being-audio --ref main -f product=livecut-redux`. Use the catalogue slug for the product you are shipping.
3. GitHub builds and tests all three platforms, signs and installs their packages, checks the installed VST3 publisher and host class IDs, opens the plug-ins in pluginval, and verifies the Windows uninstaller. macOS also validates the Audio Unit.
4. When all three packages pass, the workflow verifies provenance, publishes the product tag and uploads its catalogue record. The website discovers the release automatically.

5. After publication, **Verify live downloads** opens the production site in Chromium and waits for the product card to show the published version. It clicks the macOS, Windows and Ubuntu links, checks the downloaded bytes against the attested release manifest, and runs `gh release verify-asset` on each file. The job log records the filename, size and verified SHA-256 for each platform.

A red download check means the release is already published. After fixing the site, rerun the failed job or dispatch **Verify live downloads** for the tag currently served by the site. This dispatch verifies the published files using read access to the release and its attestations.

```sh
gh workflow run verify-downloads.yml --ref main -f tag=pole-field/v0.1.0
```

Each release contains a universal macOS PKG, a Windows x64 installer, an Ubuntu 26.04 x86_64 archive, SHA256SUMS and release.json. Verify a downloaded file with `gh attestation verify FILE -R seandearnaley/human-being-audio`. Clone the source at the commit recorded in release.json as described in the repository README.

## Signing credentials and access

Contributors need only the development toolchain. Signing runs on disposable GitHub-hosted machines. The **release** environment accepts only the main branch. Maintainers with workflow access can dispatch a release from any machine.

| Platform | What the workflow uses | What maintainers renew |
| --- | --- | --- |
| Apple | Developer ID Application signs the VST3, AU and app. Developer ID Installer signs the PKG. App Store Connect submits the PKG for notarization, then the workflow staples and checks the ticket. | Replace the two base64 PKCS#12 exports, their shared export password and identity names when the certificates change. Replace the notary key, key ID and issuer ID if rotated. |
| Windows | GitHub OIDC authenticates to Azure Artifact Signing. Its signer role covers the selected Public Trust profile. Microsoft timestamps the standalone EXE, the VST3 binary, the uninstaller and the installer. NSIS builds the installer. | Maintain the Azure identity validation and certificate profile. The environment contains Azure resource identifiers. Microsoft rotates the short-lived signing certificates. |
| Ubuntu | GitHub signs provenance for the final archive and the other release assets. | GitHub issues the workflow identity for each run. |

To set the Apple export password, run `gh secret set MACOS_P12_PASSWORD --env release -R seandearnaley/human-being-audio` and enter it at the hidden prompt. The other Apple secret names are in the signing step of the workflow.

A packaging failure prevents publication. Open the failed step in Actions. After correcting an external credential, use **Re-run failed jobs**. A source fix needs a fresh run from the corrected commit. A previously created draft or tag stops another run for that version.

[Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution) · [Microsoft Artifact Signing](https://github.com/Azure/artifact-signing-action) · [NSIS licensing](https://nsis.sourceforge.io/License) · [GitHub artifact attestations](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations)
