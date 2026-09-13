"""Compile selected production loop/HTTP callbacks without the full Arduino UI.

The callbacks are extracted at test time so these tests exercise the actual
clock ordering and upload admission, rather than a second implementation.
"""
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[1]
out = pathlib.Path(sys.argv[1])
main = (root / "v5/MilestoneV5Main/MilestoneV5Main.ino").read_text()
portal = (root / "v5/MilestoneV5Main/V5Portal.h").read_text()

start = main.rindex("  serviceCompanion(millis());")
end = main.index("  delay(1);", start)
(out / "main_link_tail.inc").write_text(main[start:end])
start = main.index("  serviceCompanion(millis());\n  portal.service();")
end = main.index("  if (portal.rescanRequested)", start)
(out / "main_link_portal.inc").write_text(main[start:end])

start = portal.index('    server.on("/api/sync/data"')
end = portal.index('    server.on(\n        "/api/sync/upload"', start)
(out / "sync_raw_route.inc").write_text(portal[start:end])

def method(signature):
    start = portal.index(signature)
    brace = portal.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (portal[end] == "{") - (portal[end] == "}")
        end += 1
    return portal[start:end] + "\n"

methods = method("  static bool unsignedInteger(") + method("  void sendSyncStatus()")
if "  String syncUploadBlockReason()" in portal:
    methods += method("  String syncUploadBlockReason()")
(out / "sync_raw_methods.inc").write_text(methods)
