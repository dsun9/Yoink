# Project package fixtures

The storage tests construct their `.pointy` archives in a unique owned test directory and remove only files created by that operation. No tracked source fixture or reference checkout is moved.

`empty-v1.json` is a minimal schema-1 manifest with eight empty lists. Round-trip tests use owned copies of the font fixtures, Noto Color Emoji, and a generated SVG; the entire owned source directory is moved before reopen. Malformed archives are generated in the test process so central-directory declarations, symlink attributes, ZIP names, hashes, collection faces, and actual embedding permissions can be changed independently without committing opaque binary files.

Font fixture attribution is in `../fonts/OFL.txt`; Noto attribution is in `resources/licenses/NotoEmoji-OFL.txt` at the repository root.
