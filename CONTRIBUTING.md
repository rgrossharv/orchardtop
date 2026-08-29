# Help with OrchardTop

Thank you for helping.

## Before you change code

Open an issue for a big change. Explain the problem in plain words. This helps
people agree on the goal before anyone spends a lot of time.

Small bug fixes can go straight into a pull request.

## Pull requests

- Keep one pull request about one thing.
- Say what changed and why.
- Say which Mac and macOS version you tested.
- Run `make` before you send the pull request.
- Run `git diff --check` too.
- Add a screenshot when the screen changed.

## AI-made code

AI help is allowed. Say when you used it.

You are still responsible for the code. Read it. Test it. Do not send code you
cannot explain.

OrchardTop itself began as a vibe-coded fork. Being honest about that is part
of this project.

## Code style

This code came from btop. Please keep its basic style:

- Use tabs. A tab is four spaces wide.
- Use `and`, `or`, and `not` in C++.
- Use clear names.
- Add comments when the reason for code is not easy to see.
- Use the included `fmt` library for formatted text.
- Do not guess at hardware numbers. Hide a number when macOS cannot give a
  trustworthy value.

## License

New code must work with the Apache License 2.0 used by this project. Keep old
copyright and license notes. Mark files when you change them.
