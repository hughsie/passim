# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

## Reporting a Vulnerability

If you find a vulnerability in Passim you should let us know using a
[private vulnerability disclosure](https://github.com/hughsie/passim/security) on GitHub,
with a description of the issue, the steps you took to find the issue, affected versions, and,
if known, mitigations for the issue.

Failing that, please report the issue against the `passim` component in Red Hat bugzilla, with the
security checkbox set. You should get a response within 3 days. We have no bug bounty program,
but we're happy to credit you in updates if this is what you would like us to do.

Security bugs found or assisted by AI tools must also include two additional things:

* A reproducer that clearly shows the vulnerability on an unmodified passim binary that is running
  with all the usual OS level protections, e.g. running `passim.service` as an auto-launched
  systemd service.

* A pull request that fixes the security vulnerability and adds unit tests; the logic being that
  if you're using an autonomous system to find a theoretical issue, you should also have the common
  courtesy to submit a PR that fixes the vulnerability.

Additionally, we will not issue CVEs or GHSA IDs for AI-discovered security issues lower than
moderate -- doing so offers nothing to the open source community (anyone can use similar tools to
find the same issue) and adds a huge burden on the existing maintenance team.

The CVSS scoring of reported bugs is often wildly inflated by AI tools (for various reasons), and
the actual CVSS scoring may be modified at the Passim maintainers discretion.

So-called "low severity" security bugs or "hardening enhancements" will be treated as normal issues
and will be fixed as part of the next tarball release with no coordinated disclosure process.
