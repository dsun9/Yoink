# Keep the bounded pixel kernel optimized even in Debug. Shaping, layout,
# allocation, native input and all callers retain their normal Debug flags.
# This directory scope removes the conflicting runtime/optimization flags only
# for this small target, leaving symbols available for diagnosing the kernel.
add_subdirectory(src/media/bitmap_kernel)
target_link_libraries(pointy_assets PRIVATE pointy_bitmap_kernel)
