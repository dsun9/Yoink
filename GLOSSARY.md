# Yoink

Yoink displays movable sprites over other applications and reveals
previews from screen-edge anchors.

## Language

**Sprite grab area**:
The area of a displayed sprite or anchor preview that accepts sprite mouse
gestures. Transparent space outside this area passes input through.
_Avoid_: Bounding box, when referring to the area that accepts input

**Sprite silhouette**:
The shape of a sprite's visible artwork, including separate visible parts and
the boundaries of transparent holes.

**Anchor activation target**:
The small area at an anchor that reveals its preview and accepts the initial
pull gesture. It is separate from the preview's sprite grab area.

**Input pass-through**:
Delivery of the original mouse input to the underlying sprite or application
when the pointer is outside the top sprite's grab area.
_Avoid_: Ignoring a click, replaying a click

**Item**:
A reusable text, emoji or image entry in a project's anchor list.
_Avoid_: Sprite, when referring to the reusable entry

**Anchor list**:
The ordered collection of items assigned to one of the eight screen positions,
shared across displays.

**Default item**:
The item selected for future pulls from an anchor list.
_Avoid_: Selected item, when referring to the default for future pulls

**Replacement drawer**:
The menu of items from a temporary sprite's originating anchor list, used to
choose replacement content for that sprite.
_Avoid_: Settings menu, when referring to sprite replacement
