# PointyPointer alteration to SDL_ttf a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b.
# Fallback replaces complete HarfBuzz clusters, including default ignorables;
# the final cluster extends to text length rather than its first codepoint.
# Preserve upstream attribution/license in a generated copy, never in overrides.
file(READ "${sdl_ttf_SOURCE_DIR}/src/SDL_ttf.c" pp_ttf_original)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sdl_ttf_SOURCE_DIR}/src/SDL_ttf.c")
string(REPLACE "\r\n" "\n" pp_ttf_original "${pp_ttf_original}")
string(SHA256 pp_ttf_hash "${pp_ttf_original}")
if(NOT pp_ttf_hash STREQUAL "25a42804b18809e5c4b2eb8ed787701551d0c680aff774b7d8c54486c0d42d38")
  message(FATAL_ERROR "SDL_ttf source differs from pin; refusing cluster patch")
endif()
set(pp_ttf_old_end [=[    if (stop < positions->len && positions->pos[stop].offset > max_offset) {
        max_offset = positions->pos[stop].offset;
    } else {
        const char *last_text = text + max_offset;
        SDL_StepUTF8(&last_text, NULL);
        max_offset = (int)(uintptr_t)(last_text - text);
    }]=])
set(pp_ttf_new_end [=[    /* PointyPointer: use the next actual cluster boundary (also for RTL),
     * or all remaining bytes for the final cluster, including ignorables. */
    int end_offset = (int)length;
    for (int i = 0; i < positions->len; ++i) {
        if (positions->pos[i].offset > max_offset && positions->pos[i].offset < end_offset) {
            end_offset = positions->pos[i].offset;
        }
    }
    max_offset = end_offset;]=])
set(pp_ttf_old_test [=[            if (pos->index == 0) {
                complete = false;]=])
set(pp_ttf_new_test [=[            /* PointyPointer: if any glyph in a shaped cluster is missing,
             * fallback owns every glyph in that cluster, not just tofu. */
            bool cluster_missing = false;
            for (int j = i; j >= 0 && positions->pos[j].offset == pos->offset; --j) {
                cluster_missing |= positions->pos[j].index == 0;
            }
            for (int j = i + 1; j < positions->len && positions->pos[j].offset == pos->offset; ++j) {
                cluster_missing |= positions->pos[j].index == 0;
            }
            if (cluster_missing) {
                complete = false;]=])
set(pp_ttf_old_color_include "#include FT_OUTLINE_H")
set(pp_ttf_new_color_include "#include FT_OUTLINE_H\n#include FT_COLOR_H")
set(pp_ttf_old_color_metrics [=[    // Get the glyph metrics, always needed
    if (cached->stored == 0) {]=])
set(pp_ttf_new_color_metrics [=[    /* PointyPointer: COLR0 base outlines can be empty. Metrics must describe
     * the composited layers or the text engine omits the glyph completely.
     * Ask the existing FreeType compositor, only for actual COLR0 glyphs. */
    if (cached->stored == 0) {
        FT_LayerIterator layers = {0};
        FT_UInt layer, palette;
        if (FT_Get_Color_Glyph_Layer(font->face, cached->index, &layer, &palette, &layers)) {
            error = FT_Load_Glyph(font->face, cached->index, ft_load | FT_LOAD_COLOR);
            if (!error) error = FT_Render_Glyph(font->face->glyph, FT_RENDER_MODE_NORMAL);
            if (error) return TTF_SetFTError("COLR0 glyph metrics failed", error);
            slot = font->face->glyph;
        }
    }

    // Get the glyph metrics, always needed
    if (cached->stored == 0) {]=])
set(pp_ttf_patched "${pp_ttf_original}")
foreach(part end test color_include color_metrics)
  string(FIND "${pp_ttf_patched}" "${pp_ttf_old_${part}}" pp_ttf_position)
  if(pp_ttf_position LESS 0)
    message(FATAL_ERROR "Missing SDL_ttf cluster patch anchor ${part}")
  endif()
  string(REPLACE "${pp_ttf_old_${part}}" "${pp_ttf_new_${part}}" pp_ttf_patched "${pp_ttf_patched}")
endforeach()
set(pp_ttf_generated "${CMAKE_CURRENT_BINARY_DIR}/patched-sdl-ttf/SDL_ttf.c")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/patched-sdl-ttf")
file(CONFIGURE OUTPUT "${pp_ttf_generated}" CONTENT "${pp_ttf_patched}" @ONLY)
get_target_property(pp_ttf_sources SDL3_ttf-static SOURCES)
set(pp_ttf_replaced FALSE)
set(pp_ttf_patched_sources "")
foreach(source IN LISTS pp_ttf_sources)
  if(source STREQUAL "src/SDL_ttf.c" OR source STREQUAL "${sdl_ttf_SOURCE_DIR}/src/SDL_ttf.c")
    list(APPEND pp_ttf_patched_sources "${pp_ttf_generated}")
    set(pp_ttf_replaced TRUE)
  else()
    list(APPEND pp_ttf_patched_sources "${source}")
  endif()
endforeach()
if(NOT pp_ttf_replaced)
  message(FATAL_ERROR "SDL_ttf target lacks the expected source")
endif()
set_property(TARGET SDL3_ttf-static PROPERTY SOURCES "${pp_ttf_patched_sources}")
target_include_directories(SDL3_ttf-static PRIVATE "${sdl_ttf_SOURCE_DIR}/src")
