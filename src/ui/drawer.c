#include "../core/geometry_internal.h"
#include "backend.h"
#include "drawer_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void release_thumbnails(PPDrawer *d) {
  memset(d->textures,0,sizeof(d->textures));
  for(size_t i=0;i<8;i++) {
    pp_ui_preview_release(&d->thumbnails[i].retained);
    pp_item_snapshot_release(d->thumbnails[i].snapshot);
    d->thumbnails[i]=(PPDrawerThumbnail){0};
  }
}
static void close_backend(PPDrawer *d) {
  if (!d->backend)
    return;
  d->focus_id = d->backend->focus_id;
  if (d->backend->visible)
    pp_platform_drawer_close(d->restore_focus);
  release_thumbnails(d);
  pp_ui_backend_destroy(d->backend);
  d->backend = NULL;
}
bool pp_drawer_cache_warm(PPDrawerCache *cache,const PPDisplay *display,PPError *error) {
  if (!cache || cache->in_use || !display) return false;
  if (cache->backend) return true;
  PPUIWindowOptions options={.title="Replace sprite",
    .geometry={display->bounds.x,display->bounds.y,340,360},
    .scale=(float)display->desktop_units_per_dip,.compact=true};
  return pp_ui_backend_create(&cache->backend,&options,error);
}
void pp_drawer_cache_destroy(PPDrawerCache *cache) {
  if (!cache) return;
  pp_ui_backend_destroy(cache->backend);
  cache->backend=NULL;
}
bool pp_drawer_use_cache(PPDrawer *d,PPDrawerCache *cache,PPError *error) {
  if (!d || !cache || cache->in_use || d->backend) return false;
  if (cache->backend && !pp_ui_backend_reset(cache->backend,error)) {
    pp_drawer_cache_destroy(cache); return false;
  }
  d->cache=cache; cache->in_use=true;
  d->backend=cache->backend; cache->backend=NULL;
  if (d->backend) {
    d->scale=d->backend->window_scale;
    d->raster_scale=d->backend->scale;
  }
  return true;
}

bool pp_drawer_valid(const PPDrawer *d) {
  return d && d->generation == pp_app_generation(d->app) &&
         pp_scene_get(pp_app_scene(d->app), d->instance_id);
}
size_t pp_drawer_count(const PPDrawer *d) {
  const PPAnchorList *list =
      pp_drawer_valid(d) ? pp_project_anchor(pp_app_project(d->app), d->anchor)
                         : NULL;
  return list ? list->count : 0;
}
bool pp_drawer_open(PPDrawer **out, PPApp *app, uint64_t id, PPError *error) {
  if (out)
    *out = NULL;
  const PPSceneInstance *instance =
      app ? pp_scene_get(pp_app_scene(app), id) : NULL;
  if (!out || !instance) {
    if (error)
      *error = (PPError){PP_ERROR_ARGUMENT, "Sprite is no longer available"};
    return false;
  }
  PPDrawer *d = calloc(1, sizeof(*d));
  if (!d) {
    if (error)
      *error = (PPError){PP_ERROR_MEMORY, "Cannot allocate item drawer"};
    return false;
  }
  d->app = app;
  d->generation = pp_app_generation(app);
  d->instance_id = id;
  d->anchor = instance->origin_anchor;
  d->restore_focus = true;
  d->scale = 1;
  d->bounds = (PPRect){instance->pose.center.x + 16,
                       instance->pose.center.y + 16, 340, 360};
  *out = d;
  return true;
}
bool pp_drawer_select(PPDrawer *d, uint64_t item_id, double now,
                      PPError *error) {
  if (!pp_drawer_valid(d))
    return false;
  const PPAnchorList *list =
      pp_project_anchor(pp_app_project(d->app), d->anchor);
  for (size_t i = 0; i < list->count; i++)
    if (list->item_ids[i] == item_id) {
      PPItemSnapshot *snapshot=pp_project_snapshot(pp_app_project(d->app),item_id);
      bool ok=pp_scene_replace_snapshot(pp_app_scene(d->app),d->instance_id,snapshot,now,error);
      pp_item_snapshot_release(snapshot);
      if (ok)
        d->dismissed = true;
      return ok;
    }
  if (error)
    *error = (PPError){PP_ERROR_ARGUMENT,
                       "This item was removed. Choose another item."};
  return false;
}
void pp_drawer_layout(PPDrawer *d, const PPDisplay *displays, size_t count) {
  if (!pp_drawer_valid(d)) {
    d->dismissed = true;
    return;
  }
  const PPSceneInstance *instance =
      pp_scene_get(pp_app_scene(d->app), d->instance_id);
  const PPDisplay *display =
      pp_display_at(displays, count, instance->pose.center);
  if (!display) {
    d->dismissed = true;
    return;
  }
  double scale = display->desktop_units_per_dip;
  bool scale_changed = d->scale != (float)scale ||
                       d->raster_scale != (float)display->drawable_pixels_per_dip;
  d->scale = (float)scale;
  d->raster_scale = (float)display->drawable_pixels_per_dip;
  PPRect b = display->bounds;
  size_t choices = pp_drawer_count(d);
  double list_height = choices ? 24 + choices * 62 - 6 : 72;
  double height = 66 + list_height + (d->diagnostic[0] ? 44 : 0);
  d->bounds.w = fmin(340 * scale, b.w);
  d->bounds.h = fmin(fmin(360, height) * scale, b.h);
  d->bounds.x = fmax(
      b.x, fmin(instance->pose.center.x + 16 * scale, b.x + b.w - d->bounds.w));
  d->bounds.y = fmax(
      b.y, fmin(instance->pose.center.y + 16 * scale, b.y + b.h - d->bounds.h));
  if (d->backend && scale_changed) {
    /* A cached backend has not acquired focus before its first show. Only a
     * previously visible drawer can lose focus during a density transition. */
    if (d->backend->visible && !d->render_hidden &&
        !(SDL_GetWindowFlags(d->backend->window) & SDL_WINDOW_INPUT_FOCUS))
      d->dismissed = true;
    close_backend(d);
  }
  if (d->backend) {
    SDL_SetWindowPosition(d->backend->window, (int)d->bounds.x,
                          (int)d->bounds.y);
    SDL_SetWindowSize(d->backend->window, (int)d->bounds.w, (int)d->bounds.h);
  }
}
void pp_drawer_event(PPDrawer *d, const PPPlatformEvent *event) {
  if (!d || !event)
    return;
  uint32_t id = pp_ui_backend_window_id(d->backend);
  if (event->kind == PP_EVENT_BUTTON_DOWN && event->window_id != id) {
    d->dismissed = true;
    /* Native close restores only while this drawer still owns foreground.
     * If the original outside click has already activated its target, that
     * target keeps focus. If it has not, hide must not pick an arbitrary
     * window while the original input is still on its way to the target. */
    return;
  }
  if (!id || event->window_id != id)
    return;
  if (event->kind == PP_EVENT_WINDOW_CLOSE ||
      (event->kind == PP_EVENT_KEY_DOWN && event->key == PP_KEY_ESCAPE))
    d->dismissed = true;
  if (event->kind == PP_EVENT_FOCUS_LOST) {
    d->dismissed = true;
    d->restore_focus = false;
  }
  pp_ui_backend_event(d->backend, event);
}
static bool thumbnail(PPDrawer *d, const PPItem *item, size_t slot,
                      struct nk_rect bounds, PPError *error) {
  PPItemSnapshot *snapshot =
      pp_project_snapshot(pp_app_project(d->app), item->id);
  for(size_t i=slot+1;i<8;i++)
    if(d->thumbnails[i].snapshot&&pp_item_snapshot_item(d->thumbnails[i].snapshot)->id==item->id) {
      PPDrawerThumbnail swap=d->thumbnails[slot];d->thumbnails[slot]=d->thumbnails[i];d->thumbnails[i]=swap;
      unsigned texture=d->textures[slot];d->textures[slot]=d->textures[i];d->textures[i]=texture;break;
    }
  PPDrawerThumbnail *cached=&d->thumbnails[slot];
  double next;bool ok=pp_ui_preview_update(d->backend,d->app,&cached->retained,snapshot,
      48,0,1,d->backend->scale,0,&next,error);
  pp_item_snapshot_retain(snapshot);pp_item_snapshot_release(cached->snapshot);cached->snapshot=snapshot;
  d->textures[slot]=cached->retained.texture;cached->width=cached->retained.width;cached->height=cached->retained.height;
  cached->scale=(float)cached->retained.density;
  if (!SDL_GL_MakeCurrent(d->backend->window, d->backend->gl_context)) {
    if (error)
      *error =
          (PPError){PP_ERROR_RENDER, "Cannot restore drawer graphics context"};
    ok = false;
  }
  if (d->textures[slot]&&cached->width&&cached->height) {
    float factor=fminf(bounds.w/cached->width,bounds.h/cached->height);
    float w=cached->width*factor,h=cached->height*factor;
    struct nk_rect image_bounds=nk_rect(bounds.x+(bounds.w-w)/2,bounds.y+(bounds.h-h)/2,w,h);
    nk_draw_image(nk_window_get_canvas(&d->backend->context),image_bounds,
      &(struct nk_image){.handle=nk_handle_id((int)d->textures[slot])},nk_rgba(255,255,255,255));
  }
  pp_item_snapshot_release(snapshot);
  return ok;
}
static void row_label(PPUIBackend *ui, const PPItem *item, size_t index,
                      char *label, size_t capacity) {
  if (item->kind != PP_ITEM_TEXT) {
    snprintf(label, capacity, "Image %zu", index + 1);
    return;
  }
  pp_ui_label_text(ui->body_font, item->text, PP_UI_LABEL_PRESERVE_WHITESPACE,
                    label, capacity);
  if (!label[0]) snprintf(label, capacity, "Text %zu", index + 1);
}
static bool drawer_row(PPDrawer *d, const PPItem *item, size_t index,
                       size_t *slot, PPError *error) {
  PPUIBackend *ui = d->backend;
  struct nk_context *ctx = &ui->context;
  char id[48], label[160];
  snprintf(id, sizeof(id), "drawer-item-%llu", (unsigned long long)item->id);
  row_label(ui, item, index, label, sizeof(label));
  nk_layout_row_dynamic(ctx, 56, 1);
  struct nk_rect bounds = nk_widget_bounds(ctx);
  bool clicked = pp_ui_select(ui, id, "", pp_ui_focused(ui, id), true);
  if (ui->control_count)
    snprintf(ui->controls[ui->control_count - 1].label,
             sizeof(ui->controls[0].label), "%s", label);
  struct nk_rect clip = ctx->current->layout->clip;
  if (bounds.y + bounds.h > clip.y && bounds.y < clip.y + clip.h && *slot < 8) {
    if (!thumbnail(d, item, (*slot)++,
        nk_rect(bounds.x + 6, bounds.y + 4, 48, 48), error))
      snprintf(d->diagnostic, sizeof(d->diagnostic), "%s", error->message);
  }
  struct nk_rect text_bounds = nk_rect(bounds.x + 64, bounds.y + 8,
                                       fmaxf(0, bounds.w - 72), 22);
  const struct nk_user_font *font = &ui->body_font->handle;
  size_t length = pp_ui_fit_label(font, label, sizeof(label), text_bounds.w);
  bool pressed = nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT) &&
      nk_input_is_mouse_hovering_rect(&ctx->input, bounds) &&
      nk_input_is_mouse_hovering_rect(&ctx->input, clip);
  struct nk_color background = pressed ? pp_ui_theme.accent :
      pp_ui_focused(ui, id) || nk_input_is_mouse_hovering_rect(&ctx->input, bounds)
          ? pp_ui_theme.selection : pp_ui_theme.field;
  struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
  nk_draw_text(canvas, text_bounds, label, (int)length, font,
      background, pressed ? pp_ui_theme.surface : pp_ui_theme.ink);
  nk_draw_text(canvas, nk_rect(text_bounds.x, bounds.y + 31, text_bounds.w, 18),
      item->kind == PP_ITEM_TEXT ? "Text" : "Image",
      item->kind == PP_ITEM_TEXT ? 4 : 5, &ui->utility_font->handle,
      background, pressed ? pp_ui_theme.surface : pp_ui_theme.muted);
  return clicked;
}
static void navigate_rows(PPDrawer *d) {
  PPUIBackend *ui = d->backend;
  const PPAnchorList *list = pp_project_anchor(pp_app_project(d->app), d->anchor);
  if (!list->count) return;
  size_t focused = list->count;
  char id[48];
  for (size_t i = 0; i < list->count; i++) {
    snprintf(id, sizeof(id), "drawer-item-%llu", (unsigned long long)list->item_ids[i]);
    if (pp_ui_focused(ui, id)) { focused = i; break; }
  }
  if (focused == list->count && ui->focus_id &&
      !pp_ui_focused(ui, "drawer-close")) {
    /* A queued activation belonged to the removed item. Moving focus to a
     * remaining choice must not turn that activation into a replacement. */
    ui->activate = false;
    snprintf(d->diagnostic, sizeof(d->diagnostic),
             "This item was removed. Choose another item.");
  }
  size_t next = focused;
  switch (ui->navigation) {
  case PP_KEY_DOWN: next = focused < list->count - 1 ? focused + 1 :
      focused == list->count ? 0 : focused; break;
  case PP_KEY_UP: next = focused == list->count ? list->count - 1 :
      focused ? focused - 1 : 0; break;
  case PP_KEY_HOME: next = 0; break;
  case PP_KEY_END: next = list->count - 1; break;
  default:
    if (!ui->focus_id || (focused == list->count &&
        !pp_ui_focused(ui, "drawer-close"))) next = 0;
    break;
  }
  if (next < list->count && next != focused) {
    snprintf(id, sizeof(id), "drawer-item-%llu", (unsigned long long)list->item_ids[next]);
    pp_ui_focus(ui, id);
  }
}
bool pp_drawer_draw(PPDrawer *d, double now, PPError *error) {
  if (!pp_drawer_valid(d) || d->dismissed) {
    release_thumbnails(d);
    d->dismissed = true;
    return true;
  }
  if (!d->backend) {
    PPUIWindowOptions options = {.title = "Replace sprite",
                                 .geometry = {d->bounds.x, d->bounds.y,
                                              d->bounds.w / d->scale,
                                              d->bounds.h / d->scale},
                                 .scale = d->scale,
                                 .compact = true};
    if (!pp_ui_backend_create(&d->backend, &options, error))
      return false;
    d->backend->focus_id = d->focus_id;
    d->backend->keyboard_focus = d->focus_id != 0;
  }
  PPUIBackend *ui = d->backend;
  ui->content_sources=pp_app_content_sources(d->app,error);ui->text_assets=pp_app_assets(d->app);
  if(!ui->content_sources)return false;
  if (!pp_ui_begin(ui, error))
    return false;
  navigate_rows(d);
  struct nk_context *ctx = &ui->context;
  size_t slot=0;
  if (nk_begin(ctx, "Replace sprite", nk_rect(0, 0, ui->width, ui->height),
               NK_WINDOW_NO_SCROLLBAR)) {
    float header_widths[] = {
        fmaxf(40, nk_window_get_content_region(ctx).w - 78), 72};
    nk_layout_row(ctx, NK_STATIC, pp_ui_theme.row_height, 2, header_widths);
    nk_style_push_font(ctx, &ui->heading_font->handle);
    nk_label(ctx, "Replace sprite", NK_TEXT_LEFT);
    nk_style_pop_font(ctx);
    if (pp_ui_button(ui, "drawer-close", "Close", true))
      d->dismissed = true;
    nk_layout_row_dynamic(ctx,
        fmaxf(40, ui->height - 66 - (d->diagnostic[0] ? 44 : 0)), 1);
    struct nk_rect viewport = nk_widget_bounds(ctx);
    size_t first = ui->control_count;
    size_t choices = pp_drawer_count(d);
    double list_height = choices ? 24 + choices * 62 - 6 : 72;
    bool overflow = list_height > viewport.h;
    if (!overflow)
      nk_group_set_scroll(ctx, "drawer-items", 0, 0);
    if (nk_group_begin(ctx, "drawer-items",
        overflow ? 0 : NK_WINDOW_NO_SCROLLBAR)) {
      const PPAnchorList *list =
          pp_project_anchor(pp_app_project(d->app), d->anchor);
      if (!list->count) {
        nk_layout_row_dynamic(ctx, 48, 1);
        nk_label_wrap(ctx, "No items at this position. Add items in Settings "
                           "to choose a replacement.");
      }
      for (size_t i = 0; i < list->count; i++) {
        const PPItem *item =
            pp_project_item(pp_app_project(d->app), list->item_ids[i]);
        if (drawer_row(d, item, i, &slot, error)) {
          if (!pp_drawer_select(d, item->id, now, error))
            snprintf(d->diagnostic, sizeof(d->diagnostic), "%s",
                     error->message);
          break;
        }
      }
      nk_group_end(ctx);
    }
    if (overflow)
      pp_ui_reveal_focus(ui, "drawer-items", viewport, first);
    pp_ui_error(ui, d->diagnostic);
  }
  nk_end(ctx);
  for(size_t i=slot;i<8;i++) {
    pp_ui_preview_release(&d->thumbnails[i].retained);pp_item_snapshot_release(d->thumbnails[i].snapshot);
    d->thumbnails[i]=(PPDrawerThumbnail){0};d->textures[i]=0;
  }
  if (!pp_ui_end(ui, error))
    return false;
  if (!d->render_hidden && !d->dismissed && !ui->visible) {
    pp_platform_drawer_window(pp_ui_backend_window_id(ui));
    pp_ui_show(ui, true);
    SDL_RaiseWindow(ui->window);
  }
  return true;
}
void pp_drawer_close(PPDrawer *d) {
  if (!d)
    return;
  if (d->cache) {
    if (d->backend && d->backend->visible) pp_platform_drawer_close(d->restore_focus);
    if (d->backend) pp_ui_show(d->backend,false);
    release_thumbnails(d);
    d->cache->backend=d->backend;
    d->cache->in_use=false;
    d->backend=NULL;
  } else close_backend(d);
  free(d);
}
