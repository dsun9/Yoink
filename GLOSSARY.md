# PointyPointer

PointyPointer displays movable sprites over other applications and reveals
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
