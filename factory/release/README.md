# Release a studio product

1. Merge the product changes with its new three-part version in the product’s top-level CMakeLists.txt. This version becomes the plug-in version, package filenames and product-scoped release tag.
2. In GitHub Actions, open **Studio release**, select **Run workflow**, keep the branch on **main** and choose the product. From any authenticated terminal, the equivalent command is `gh workflow run release.yml -R seandearnaley/human-being-audio --ref main -f product=livecut-redux`. Use `tract-form` for the synth.
3. A release reviewer approves access to the **release** environment. GitHub builds each platform, runs CTest, signs and packages the binaries, then installs and validates the packaged VST3 with editor tests. macOS also validates the installed AU. Windows checks the signatures and uninstalls the product.
4. Review the draft under **Releases**. For the first release or an installer change, exercise the standalone and a saved DAW session, including any previous public install. Add the musician-facing notes and publish when satisfied. Each product has its own tag and version. The website reads public releases automatically.

The draft contains a universal macOS PKG, a Windows x64 installer, an Ubuntu 26.04 x86_64 archive, SHA256SUMS and release.json. Verify a downloaded file with `gh attestation verify FILE -R seandearnaley/human-being-audio`. Clone the source at the commit recorded in release.json with submodules as described in the repository README.

## Signing credentials and access

Contributors need only the development toolchain. Signing runs on disposable GitHub-hosted machines. Under **Settings → Environments → release**, grant approval access to the maintainers who can sign a release. The environment accepts the main branch.

| Platform | What the workflow uses | What maintainers renew |
| --- | --- | --- |
| Apple | Developer ID Application signs the VST3, AU and app. Developer ID Installer signs the PKG. App Store Connect submits the PKG for notarization, then the workflow staples and checks the ticket. | Replace the two base64 PKCS#12 exports, their shared export password and identity names when the certificates change. Replace the notary key, key ID and issuer ID if rotated. |
| Windows | GitHub OIDC authenticates to Azure Artifact Signing. Its signer role covers the selected Public Trust profile. Microsoft timestamps the standalone EXE, the VST3 binary, the uninstaller and the installer. NSIS builds the installer. | Maintain the Azure identity validation and certificate profile. The environment contains Azure resource identifiers. Microsoft rotates the short-lived signing certificates. |
| Ubuntu | GitHub signs provenance for the final archive and the other release assets. | GitHub issues the workflow identity for each run. |

To set the Apple export password, run `gh secret set MACOS_P12_PASSWORD --env release -R seandearnaley/human-being-audio` and enter it at the hidden prompt. The other Apple secret names are in the signing step of the workflow.

A platform failure prevents draft creation. Open the failed step in Actions. After correcting an external credential, use **Re-run failed jobs**. A source fix needs a fresh run from the corrected commit. A previously created draft or tag stops another run for that version.

[Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution) · [Microsoft Artifact Signing](https://github.com/Azure/artifact-signing-action) · [NSIS licensing](https://nsis.sourceforge.io/License) · [GitHub artifact attestations](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations)
