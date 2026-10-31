# Contributing to the closed beta

Thank you for helping improve NFSU2 PSP! The beta repository contains the
generator, conversion tools, documentation, tests, and a prebuilt game
executable. The game's main source code is not in this repository. Contributions
here can improve the generator, tools, documentation, and testing; please use an
issue to report or discuss game bugs.

## Language and published files

Use English for documentation, code comments, interface text, error messages,
CLI help and new commit subjects. Use neutral project terminology in published
files, and keep local absolute workspace paths out of shared reports. Preserve
required copyright, attribution and license notices. Historical commits and
release artifacts retain their original contents.

## Before you start

- Accept your invitation to the private beta repository.
- Search the existing issues and pull requests. For a substantial change, open
  an issue first so we can discuss the approach and avoid duplicated work.
- Keep the beta private and share it only with people already authorized to
  access it. Do not make a separate public copy of the repository.
- Never commit or attach ISOs, original game assets, extracted or generated game
  files, saves, credentials, or other private information. Use synthetic test
  data where possible, and check logs and screenshots before sharing them.

## Send a pull request

1. On the beta repository page, select **Fork** and create a private fork in
   your personal GitHub account. The fork stays private. If **Fork** is
   unavailable, ask a maintainer whether private forks are enabled.
2. Clone your fork and create a branch for your change. Make a focused change
   and commit it to that branch. Push the branch to your fork.
3. Open a pull request from your fork's branch to the beta repository's `main`
   branch. Give it a clear title and explain the problem and your solution.
   Link the relevant issue if there is one.
4. Describe how you tested the change, including your operating system and
   relevant device, and say which tests you could not run. For visual or UI
   changes, include screenshots when useful. Check that attachments do not
   contain original game data or private information.
5. A maintainer will review the change and may ask questions or request changes.
   Pushing more commits to the same branch updates the pull request. Once the
   change is reviewed and tested, a maintainer can merge it.

For native generator development and its test commands, see
[native/README.md](native/README.md). For game testing and reporting, see
[docs/TESTING.md](docs/TESTING.md). Tests are valuable, but please report their
actual results and limitations rather than assuming untested platforms work.

You can also contribute by reporting bugs, improving reproduction steps, or
testing a proposed fix; a pull request is not required to participate.
