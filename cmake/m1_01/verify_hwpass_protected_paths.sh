#!/bin/sh
# verify_hwpass_protected_paths.sh -- POST_BUILD guard protecting every
# production path covered by a hardware-accepted checkpoint (see
# documentation/hardware-acceptance/ and cmake/m1_01/hwpass_protected_paths_
# manifest.txt). For each PROTECTED path, compares its content in the current
# working tree against the ACCEPTED_CHECKPOINT git ref recorded in the
# manifest -- not against a second stored hash ledger, so there is only one
# source of truth (git itself) and no drift between a hash file and reality.
# For each FORBIDDEN_SYM, requires an nm tool + linked ELF (both optional --
# see Usage) and checks the symbol is not a defined text symbol in the link;
# this is a compiled/linked-artifact check, deliberately NOT a source-text
# grep, because a source-text match is defeated by (or falsely trips on) an
# ordinary comment that merely names a removed component, e.g. citing it as
# a prior-art naming/decoupling precedent elsewhere in the codebase.
#
# A protected path that differs from ACCEPTED_CHECKPOINT means that path's
# hardware-accepted status is no longer proven for the current tree -- the
# build fails and reports exactly which accepted subsystem(s) were touched.
# The only sanctioned way to make this pass again after an intentional change
# is to record new hardware acceptance and move ACCEPTED_CHECKPOINT to a new
# checkpoint/* tag as a reviewed, tracked edit to the manifest -- there is no
# environment-variable or flag bypass.
#
# Usage: verify_hwpass_protected_paths.sh <repo-root> [<nm-tool> <elf-path>]
#   nm-tool/elf-path are optional: if omitted, FORBIDDEN_SYM checks are
#   skipped with a warning (PROTECTED path checks still run in full) so this
#   script stays runnable standalone without a fresh build; POST_BUILD wiring
#   always supplies them.
set -u
ROOT="${1:-.}"
NM_TOOL="${2:-}"
ELF_PATH="${3:-}"
MAN="$ROOT/cmake/m1_01/hwpass_protected_paths_manifest.txt"
cd "$ROOT" 2>/dev/null || { echo "verify_hwpass_protected_paths: cannot cd to '$ROOT'" >&2; exit 2; }
command -v git >/dev/null 2>&1 || { echo "verify_hwpass_protected_paths: git not found" >&2; exit 2; }
[ -f "$MAN" ] || { echo "verify_hwpass_protected_paths: manifest not found: $MAN" >&2; exit 2; }
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "verify_hwpass_protected_paths: not a git work tree" >&2; exit 2; }

SYMS=""
if [ -n "$NM_TOOL" ] && [ -n "$ELF_PATH" ] && [ -f "$ELF_PATH" ]; then
    SYMS=$("$NM_TOOL" "$ELF_PATH" 2>/dev/null)
else
    echo "verify_hwpass_protected_paths: (nm-tool/elf-path not supplied -- FORBIDDEN_SYM checks skipped)" >&2
fi

CHECKPOINT=$(grep -E '^ACCEPTED_CHECKPOINT[[:space:]]' "$MAN" | awk '{print $2}')
[ -n "$CHECKPOINT" ] || { echo "verify_hwpass_protected_paths: no ACCEPTED_CHECKPOINT in manifest" >&2; exit 2; }
git rev-parse --verify -q "${CHECKPOINT}^{commit}" >/dev/null 2>&1 || {
    echo "verify_hwpass_protected_paths: checkpoint ref '$CHECKPOINT' does not resolve in this repo" >&2
    exit 2
}

fail=0
subsystems=""
note_subsystem() {
    case " $subsystems " in
      *" $1 "*) : ;;
      *) subsystems="$subsystems $1" ;;
    esac
}

while read -r kind sub arg; do
    case "$kind" in
      PROTECTED)
        if [ ! -e "$arg" ]; then
            echo "  FAIL [$sub] protected path no longer exists: $arg" >&2
            fail=1; note_subsystem "$sub"
            continue
        fi
        d=$(git diff --no-color --name-only "$CHECKPOINT" -- "$arg" 2>/dev/null)
        if [ -n "$d" ]; then
            echo "  FAIL [$sub] protected path changed since $CHECKPOINT:" >&2
            printf '%s\n' "$d" | sed 's/^/      /' >&2
            fail=1; note_subsystem "$sub"
        fi
        ;;
      FORBIDDEN_SYM)
        if [ -z "$SYMS" ]; then
            : # no ELF supplied this run -- already warned above, don't fail setup
        elif echo "$SYMS" | grep -qE "[0-9a-fA-F]+ [Tt] ${arg}\$"; then
            echo "  FAIL [$sub] forbidden symbol linked into the production ELF: $arg" >&2
            fail=1; note_subsystem "$sub"
        fi
        ;;
      ''|\#*|ACCEPTED_CHECKPOINT) : ;;
    esac
done < "$MAN"

if [ "$fail" = "0" ]; then
    echo "verify_hwpass_protected_paths.sh: OK -- all protected paths match accepted checkpoint $CHECKPOINT."
    exit 0
fi
echo "verify_hwpass_protected_paths.sh: FAILED -- hardware-accepted status invalidated for:$subsystems" >&2
echo "  Revert the change(s) above, or record new hardware acceptance under" >&2
echo "  documentation/hardware-acceptance/ + an annotated checkpoint/* tag, then" >&2
echo "  update ACCEPTED_CHECKPOINT (and the PROTECTED/FORBIDDEN_SRC set, if the" >&2
echo "  accepted scope changed) in cmake/m1_01/hwpass_protected_paths_manifest.txt" >&2
echo "  as a reviewed, tracked edit. There is no other way to clear this failure." >&2
exit 1
