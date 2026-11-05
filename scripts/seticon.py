#!/usr/bin/env python3

import sys


def main(argv):
    if len(argv) != 3:
        print(f"usage: {argv[0]} <image> <file>", file=sys.stderr)
        return 2

    try:
        import Cocoa
    except ImportError as error:
        print(f"could not import Cocoa: {error}", file=sys.stderr)
        return 1

    image = Cocoa.NSImage.alloc().initWithContentsOfFile_(argv[1])
    if image is None:
        print(f"could not load icon image: {argv[1]}", file=sys.stderr)
        return 1

    workspace = Cocoa.NSWorkspace.sharedWorkspace()
    if not workspace.setIcon_forFile_options_(image, argv[2], 0):
        print(f"could not set icon for file: {argv[2]}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
