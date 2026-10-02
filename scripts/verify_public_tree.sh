#!/bin/sh
set -eu

fail=0

report_matches() {
    title=$1
    shift
    if output=$("$@" 2>/dev/null) && [ -n "$output" ]; then
        echo "ERROR: $title" >&2
        echo "$output" >&2
        fail=1
    fi
}

report_matches "tracked build artifacts are present" sh -c '
    git ls-files | grep -Ei "(^|/)(build|artifacts|out|debug|release)(/|$)|\.(map|elf|hex|bin|o|a|d|su)$" || true
'

report_matches "developer-machine absolute paths are present" sh -c '
    git grep -nE "/Users/|[A-Z]:\\\\Users\\\\" -- . \
        ":(exclude)scripts/verify_public_tree.sh" \
        ":(exclude)Drivers/**" \
        ":(exclude)Middlewares/**" \
        ":(exclude)NFC/Middlewares/**" || true
'

report_matches "unfinished public-document placeholders are present" sh -c '
    git grep -nE "\[INSERT |(^|[^[:alnum:]_])link-to-|\[Maintainer Name\]" -- .github README.md README_License.md DEVELOPMENT.md CHANGELOG.md documentation || true
'

report_matches "common credential signatures are present" sh -c '
    git grep -nE "AKIA[0-9A-Z]{16}|ASIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}|AIza[0-9A-Za-z_-]{30,}|BEGIN (RSA |EC |OPENSSH |DSA )?PRIVATE KEY|client_secret[[:space:]]*[:=]" -- . \
        ":(exclude)Drivers/**" \
        ":(exclude)Middlewares/**" \
        ":(exclude)NFC/Middlewares/**" \
        ":(exclude)Esp_spi_at/esp-idf/**" || true
'

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "Public-tree hygiene: PASS"
