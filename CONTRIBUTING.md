# Contributing

Issues and pull requests are welcome.

- **Issues:** say which cellular module and co-processor version (`ATI` reply) your dongle has, and attach the device log where it helps (remove keys and passwords first).
- **Pull requests:** open them against `dev`, not `main` (`main` only receives releases). Before you do:
  - The checks in [docs/development.md](docs/development.md) → "Testing" pass, including `esphome config` on the examples.
  - Add a line under `## [Unreleased]` in `CHANGELOG.md` describing the change for people using the package. Don't change the version or date a changelog section: that's the release step.
  - Anything that polls the vehicle follows the conservative rules in [docs/development.md](docs/development.md) → "Vehicle safety".

Setup, testing, the changelog rules and how releases work are in [docs/development.md](docs/development.md).
