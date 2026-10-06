#ifndef POINTY_UI_SHAPED_EDIT_H
#define POINTY_UI_SHAPED_EDIT_H
#include "backend.h"
#include "edit_state.h"
typedef struct PPUIShapedEdit PPUIShapedEdit;
bool pp_ui_shaped_draw(PPUIBackend *ui, char *draft, int capacity,
                       struct nk_rect bounds, bool focused, bool enabled);
bool pp_ui_shaped_event(PPUIBackend *ui, const PPPlatformEvent *event);
/* A reusable reset preserves ownership unchanged if its GL context cannot bind.
 */
bool pp_ui_shaped_reset(PPUIBackend *ui);
/* Final teardown: an unbindable context reclaims its own GPU names on destroy.
 */
void pp_ui_shaped_destroy(PPUIBackend *ui);
/* Read-only instrumentation of the actual field, also used by focused tests. */
const PPUITextView *pp_ui_shaped_view(const PPUIBackend *ui);
const PPUIEditState *pp_ui_shaped_state(const PPUIBackend *ui);
#endif
