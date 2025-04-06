# CHAD

CHAD is a basic message server built with websockets using C/C++!

## Getting Started

This project manages all it's dependencies as git submodules! Once you've cloned
the repository please use:

```bash
# Clone the repository and it's dependencies
git submodule update --init --recursive
```

This downloads all the project dependencies on your computer.

Last thing you'll need is [Nix](https://nixos.org/download/). Please make sure
to enable [Flakes](https://wiki.nixos.org/wiki/Flakes). To enable flakes you'll
basically need to copy this inside `~/.config/nix/nix.conf` or
`/etc/nix/nix.conf`:

```
experimental-features = nix-command flakes
```

Now, from within the repository root simply type

```bash
nix develop
```

This will enter you inside a development shell with all the tools that we had
during development.

To run the project please read the required README of each section!

- [Frontend](client/README.md)
- [Backend](server/README.md)
