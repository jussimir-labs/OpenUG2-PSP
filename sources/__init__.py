"""Where the game's files come from: one module per console.

Each source module defines a Source with:

  name          the folder under the work folder's game/ the converters
                read (game/xbox/NFSUNDER for the Xbox's)
  available     False while the source is still being written (then
                note says why); build_game.py stops with that message
  known_isos    {sha256: description} of the images checked end to end
  file_count    how many files verify() checks
  recognise(iso)          True if the image looks like this console's
  extract(iso, dest, say) the disc's files into dest (say: progress lines)
  verify(dest)            the extracted files that differ from the
                          expected ones (an empty list: all equal)
  game_root(dest)         the folder the converters take as --game

A new console (the PS2's, for one) is a module here and a line in
SOURCES; build_game.py and the converters stay the same as long as the
module leaves the files where the converters look for them.
"""


class SourceError(Exception):
    """A clear stop in a source: the message is for the player."""


from . import ps2, xbox  # noqa: E402

SOURCES = {s.name: s for s in (xbox.Source(), ps2.Source())}
