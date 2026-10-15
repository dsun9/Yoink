# Publish sprite pixels and placement through DirectComposition

For [issue #32](https://github.com/dsun9/Yoink/issues/32), Windows retained
sprites use a stable native visual host and a tight input window. The prior
SDL window moved before its new GL image reached the compositor. The desktop
regression observed about 19 pixels of rotation displacement and 12 pixels
of zoom displacement while completed GL landmarks remained accurate.

The visual host covers the virtual desktop and stays at its accepted native
origin during ordinary interaction. Its DirectComposition visual owns the
tight logical output. Source shaders draw directly into the surface returned
by `BeginDraw` through `WGL_NV_DX_interop2`. `EndDraw` completes that update,
then one `Commit` submits the content and matching offsets. There is no second
retained GL output, GPU copy, full-frame readback, compositor wait or added
interaction sleep. The logical surface and replacement overlap retain the
existing 256 MiB GPU admission policy. DirectComposition's internal atlas
allocation is managed by Windows and is distinct from those logical pixels.

The native input window is created with `WS_EX_NOREDIRECTIONBITMAP` and wrapped
by SDL for its WGL context. It has no artwork front buffer. It omits SDL's
transparent-window mode, which enables DWM alpha hit testing. Windows instead
hit tests the existing bounded native grab region. The layered, transparent,
nonactivating visual host passes original cross-process input through to that
window or the underlying application. Raise, replacement order, hide and
destruction manage the pair. This preserves the quadtree, 1,024-rectangle cap,
animation unions and shared 8 MiB grab budget from
[ADR 0001](0001-bounded-sprite-grab-areas.md).

An integer translation commits only the visual offsets. Exposure uses the
same retained surface. Unchanged presentation also checks virtual-desktop
metrics and otherwise returns without a commit. A changed desktop layout
creates a fresh hidden host, target and visual. Its first content-bearing
commit contains offsets based on its own native origin. The source, output,
input HWND and logical pose survive. This transition may be blank until DWM
accepts the new tree; it never moves a host carrying an old-origin tree.

Backend selection probes actual register/lock/unlock/unregister compatibility
before accepting source or output. If interop is unavailable, the still-empty
input window is replaced by the existing transparent SDL presenter. Its
coherent-publication behavior remains unverified. Selection metadata is stored
in memory and the performance probe reports it through its bounded buffer.
Production selection does not synchronously write to stdout or stderr.

The owning renderer establishes one shared D3D11 device during normal runtime
startup. `InitOnceExecuteOnce` synchronizes process initialization. Per-sprite
composition devices and WGL interop handles still probe the actual current GL
context; they do not share mutable visual trees or immediate-context work.
This avoids repeating device initialization while the owner serves input.
The performance protocol includes this cost in runtime startup, then keeps all
96 cold source/output cases and tagged input deadlines unchanged.

Register/lock failures reject the update. Unlock/unregister failures receive
one bounded cleanup retry and reject that publication even if cleanup then
succeeds. Outstanding ownership is never passed back to DX. If the driver
permanently refuses cleanup, the failed record stays hidden and quarantined
until process exit, preserving its native/COM ownership rather than releasing
registered storage. Ordinary cleanup, unavailable contexts and persistent
anchor-surface reuse have separate release checks. Real device removal and
permanent driver failure are not runtime acceptance claims.

Failed native output detachment keeps its references and logical charge until
detachment or a new publication succeeds. Outstanding interop or detach
ownership survives renderer retirement and owner teardown. The process-lived
quarantine charge participates in resource reports and subsequent presenters'
256 MiB admission. A capability probe that cannot release its registration
charges its one logical pixel. Narrow native fault controls verify these
paths without simulating successful cleanup.

The selected backend has Windows/NVIDIA evidence. Simulated density and
injected virtual-desktop metrics do not establish physical mixed-display or
hotplug behavior. GL completion and returned DirectComposition commits do not
establish scanout or frame cadence. Native macOS behavior remains unverified.
The completed-output diagnostic observer is opt-in and performs no production
readback. Native pixel tests use independent Desktop Duplication captures
because GDI does not include the DirectComposition visual.
