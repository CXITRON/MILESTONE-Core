#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

require_text() {
  local file="$1"
  local text="$2"
  if ! grep -Fq -- "$text" "$file"; then
    printf 'Diagnostics contract mismatch: %s is missing %s\n' "$file" "$text" >&2
    exit 1
  fi
}

require_text CorePortal.inc 'server.on("/api/diagnostics", HTTP_GET, handleDiagnostics);'
require_text CorePortal.inc 'server.on("/api/diagnostics/clear", HTTP_POST, handleDiagnosticsClear);'
require_text CorePortal.inc '\"history_count\"'
require_text CorePortal.inc '\"events\"'
require_text PortalPage.h "api('/api/diagnostics')"
require_text PortalPage.h "api('/api/diagnostics/clear'"
require_text PortalPage.h '진단 정보 복사'
require_text PortalPage.h '진단 이력 지우기'
require_text CoreDiagnostics.inc 'MilestoneDiagnostics::History nextHistory = diagnosticHistory;'
require_text CoreDiagnostics.inc 'persistDiagnosticHistory(nextHistory)'
require_text CoreDiagnostics.inc 'diagnosticHistory = nextHistory;'
require_text CoreDiagnostics.inc 'runtimeState == RuntimeState::RUNNING_ONLINE'
require_text CoreDiagnostics.inc 'thermalSafeMode || !stableRuntime'
require_text tools/milestone-release 'find_taildrop_version_anchor'
require_text tools/milestone-release 'remote_version=$(source_version_at_ref origin/main)'
require_text tools/milestone-release 'rebase --onto origin/main "$version_anchor" main'
require_text tools/milestone-release 'refresh_installed_command'
require_text tools/milestone-release "'cd /tmp' 후 다시 실행하세요."

printf 'Diagnostics portal/API contract test passed\n'
