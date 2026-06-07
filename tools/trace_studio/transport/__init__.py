"""trace_studio.transport — frame format conversion + all-intra video encode.

(D2 local-disk capture + copyback is the planned `sink` here; the export_trace
drive path does not yet support exe-side --capture-local, so the fast path is
deferred to Phase 3 — see drive/caps.py EngineCaps.supports_capture_local.)
"""
