# Security

The application runs without elevation and targets only the same-session system `explorer.exe` process that owns `Shell_TrayWnd`. Compatibility is keyed by PE timestamp, image size, CodeView GUID, and PDB age. Unknown records fail closed.

Do not report private signing keys. For vulnerabilities, open a private security advisory in the source repository. Compatibility records are code-adjacent security data and require the same review as native hook changes.
