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
  d->bounds.w = fmin(340 * scale, b.w);
  d->bounds.h = fmin(360 * scale, b.h);
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
  struct nk_context *ctx = &ui->context;
  size_t slot=0;
  if (nk_begin(ctx, "Replace sprite", nk_rect(0, 0, ui->width, ui->height),
               NK_WINDOW_NO_SCROLLBAR)) {
    pp_ui_heading(ui, "Replace sprite");
    nk_layout_row_dynamic(ctx, 38, 1);
    nk_label_wrap(ctx, "Choose from this position's current list.");
    nk_layout_row_dynamic(ctx, fmaxf(40, ui->height - 202), 1);
    struct nk_rect viewport = nk_widget_bounds(ctx);
    size_t first = ui->control_count;
    if (nk_group_begin(ctx, "drawer-items", 0)) {
      const PPAnchorList *list =
          pp_project_anchor(pp_app_project(d->app), d->anchor);
      if (!list->count) {
        nk_layout_row_dynamic(ctx, 80, 1);
        nk_label_wrap(ctx, "No items at this position. Add items in Settings "
                           "to choose a replacement.");
      }
      for (size_t i = 0; i < list->count; i++) {
        const PPItem *item =
            pp_project_item(pp_app_project(d->app), list->item_ids[i]);
        char id[48], label[160];
        snprintf(id, sizeof(id), "drawer-item-%llu",
                 (unsigned long long)item->id);
        char notice[256];
        if (item->kind == PP_ITEM_TEXT &&
            !pp_ui_unicode_notice(ui, item->text, notice, sizeof(notice)))
          snprintf(label, sizeof(label), "%s", item->text);
        else if (item->kind == PP_ITEM_TEXT)
          snprintf(label, sizeof(label), "Text %zu", i + 1);
        else
          snprintf(label, sizeof(label), "Image %zu", i + 1);
        float widths[] = {52,
                          fmaxf(40, nk_window_get_content_region(ctx).w - 64)};
        nk_layout_row(ctx, NK_STATIC, 56, 2, widths);
        struct nk_rect image_bounds;
        enum nk_widget_layout_states visible = nk_widget(&image_bounds, ctx);
        if (visible != NK_WIDGET_INVALID && slot < 8) {
          if (!thumbnail(d, item, slot++, image_bounds, error))
            snprintf(d->diagnostic, sizeof(d->diagnostic), "%s",
                     error->message);
        }
        if (pp_ui_button(ui, id, label, true)) {
          if (!pp_drawer_select(d, item->id, now, error))
            snprintf(d->diagnostic, sizeof(d->diagnostic), "%s",
                     error->message);
          break;
        }
      }
      nk_group_end(ctx);
    }
    pp_ui_reveal_focus(ui, "drawer-items", viewport, first);
    pp_ui_error(ui, d->diagnostic);
    nk_layout_row_dynamic(ctx, pp_ui_theme.row_height, 1);
    if (pp_ui_button(ui, "drawer-close", "Close", true))
      d->dismissed = true;
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
