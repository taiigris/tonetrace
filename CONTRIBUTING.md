# Contributing

Thanks for helping with Tonetrace.

1. Open an issue first for larger changes, so we can agree on the approach.
2. Fork, create a branch, and keep the pull request focused on one thing.
3. Run `./build.sh --no-install` before you push (see the README for the VST3 SDK setup).
4. Use [conventional commit](https://www.conventionalcommits.org/) titles, e.g. `fix: keep playhead on zoom`.
   Pull requests are squash-merged, so the PR title becomes the commit message.

CI (`Build and test`) must pass and the maintainer reviews every pull request before it is merged.

Never commit model weights, audio you don't have the rights to, or credentials.
