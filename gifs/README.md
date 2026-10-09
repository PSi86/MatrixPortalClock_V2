# Example GIFs

The GIF pack (`scripts/gif_pack.py`) takes every GIF of this folder that fits
the panel; the S3 clock shows them now and then in place of its face. None is
built into the firmware. They are unchanged copies of the files named below, under the
licences given there.

| File | Size | Picture | Author and source | Licence |
| --- | --- | --- | --- | --- |
| `yule-log-fireplace.gif` | 64x32 | a log fire in a fireplace | Tidbyt, Inc., app "Yule Log" in [tidbyt/community](https://github.com/tidbyt/community/tree/main/apps/yulelog); drawn after [Pexels video 6507518](https://www.pexels.com/video/cold-relaxing-winter-photography-6507518/) | Apache License 2.0 ([text](licenses/Apache-2.0.txt)) |
| `paraland-seattle.gif` | 64x32 | Seattle skyline with the Space Needle, hand-drawn | yonodactyl, app "Paraland" in [tidbyt/community](https://github.com/tidbyt/community/tree/main/apps/paraland) | Apache License 2.0 ([text](licenses/Apache-2.0.txt)) |
| `little-runner.gif` | 32x32 | a running figure | Sudden ([User:Bibbelo](https://commons.wikimedia.org/wiki/User:Bibbelo)), [File:LittleRunner.gif](https://commons.wikimedia.org/wiki/File:LittleRunner.gif) on Wikimedia Commons | [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) |
| `neko-cat.gif` | 32x32 | the Neko desktop cat | Neko (oneko) sprites, [File:Neko animated.gif](https://commons.wikimedia.org/wiki/File:Neko_animated.gif) on Wikimedia Commons, which names no copyright holder | 3-clause BSD licence, per Wikimedia Commons ([text](licenses/BSD-3-Clause.txt)) |

`little-runner.gif` stays under CC BY-SA 4.0 wherever it is passed on.

## Your own GIFs

GIFs from other folders go into your own GIF pack, in the board's ffat
partition: list the folders, one per line, in `gif_pack.local` in the project
folder (git ignores that file; see "GIF pack" in the
[README](../README.md#gif-pack)).
A folder's `exclude.txt` (or its parent's) names files to leave out, one per
line; a line matches the end of a file's path. Many well-known GIFs belong to
their creators: keep them out of this repository, and do not publish a GIF pack
built with them.
