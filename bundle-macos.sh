#!/usr/bin/env bash
# Build a distributable photo-salon.app bundle and optional DMG.
#
# Signing / notarization are opt-in via environment variables:
#
#   CODESIGN_IDENTITY   — codesign identity string, e.g.
#                         "Developer ID Application: Your Name (TEAMID)"
#                         Use "-" for ad-hoc signing (runs on your machine only).
#
#   NOTARIZE_APPLE_ID   — Apple ID email for notarytool (requires CODESIGN_IDENTITY)
#   NOTARIZE_PASSWORD   — App-specific password (or @keychain:<item>)
#   NOTARIZE_TEAM_ID    — 10-character Apple Developer team ID
#
# Output:
#   _bundle/photo-salon.app   — the app bundle
#   _bundle/photo-salon.dmg   — disk image (if --dmg flag is given or signing occurs)
#
# Usage:
#   ./bundle-macos.sh [--dmg]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/_build"
BUNDLE_DIR="$SCRIPT_DIR/_bundle"
APP="$BUNDLE_DIR/photo-salon.app"
DMG="$BUNDLE_DIR/photo-salon.dmg"
ENTITLEMENTS="$SCRIPT_DIR/macos/entitlements.plist"

MAKE_DMG=false
for arg in "$@"; do
    [[ "$arg" == "--dmg" ]] && MAKE_DMG=true
done

# ── 1. Build ───────────────────────────────────────────────────────────────────
echo "→ Building…"
"$SCRIPT_DIR/build"

# ── 2. Locate macdeployqt ──────────────────────────────────────────────────────
MACDEPLOYQT=""
for qmake_cmd in qmake6 qmake; do
    if command -v "$qmake_cmd" &>/dev/null; then
        QT_BINS="$("$qmake_cmd" -query QT_INSTALL_BINS 2>/dev/null || true)"
        if [[ -x "$QT_BINS/macdeployqt" ]]; then
            MACDEPLOYQT="$QT_BINS/macdeployqt"
            break
        fi
    fi
done

if command -v brew &>/dev/null && [[ -z "$MACDEPLOYQT" ]]; then
    BREW_QT="$(brew --prefix qt 2>/dev/null || true)"
    [[ -x "$BREW_QT/bin/macdeployqt" ]] && MACDEPLOYQT="$BREW_QT/bin/macdeployqt"
fi

if [[ -z "$MACDEPLOYQT" ]]; then
    echo "error: macdeployqt not found. Make sure Qt is on PATH or installed via Homebrew." >&2
    exit 1
fi
echo "→ Using macdeployqt: $MACDEPLOYQT"

# ── 3. Stage app bundle ────────────────────────────────────────────────────────
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR"
cp -R "$BUILD_DIR/photo-salon.app" "$APP"

# ── 4. Deploy Qt frameworks ────────────────────────────────────────────────────
echo "→ Deploying Qt frameworks…"
if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    "$MACDEPLOYQT" "$APP" -sign-for-notarization="$CODESIGN_IDENTITY"
else
    "$MACDEPLOYQT" "$APP"
fi

# ── 5. Code sign (optional) ────────────────────────────────────────────────────
if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    echo "→ Signing with identity: $CODESIGN_IDENTITY"
    # macdeployqt -sign-for-notarization already signs the bundle, but we re-sign
    # the main executable explicitly to attach the entitlements.
    codesign \
        --force \
        --options runtime \
        --entitlements "$ENTITLEMENTS" \
        --sign "$CODESIGN_IDENTITY" \
        "$APP/Contents/MacOS/photo-salon"

    # Verify
    codesign --verify --deep --strict "$APP"
    echo "   ✓ Signature verified"
fi

# ── 6. Create DMG (when requested or when signing/notarizing) ─────────────────
if [[ "$MAKE_DMG" == true ]] || [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    echo "→ Creating DMG…"
    rm -f "$DMG"
    hdiutil create \
        -volname "Photo Salon" \
        -srcfolder "$BUNDLE_DIR" \
        -ov \
        -format UDZO \
        "$DMG"
    echo "   → $DMG"
fi

# ── 7. Notarize (optional) ─────────────────────────────────────────────────────
if [[ -n "${NOTARIZE_APPLE_ID:-}" ]]; then
    if [[ -z "${NOTARIZE_PASSWORD:-}" || -z "${NOTARIZE_TEAM_ID:-}" ]]; then
        echo "error: NOTARIZE_PASSWORD and NOTARIZE_TEAM_ID must be set alongside NOTARIZE_APPLE_ID." >&2
        exit 1
    fi
    if [[ ! -f "$DMG" ]]; then
        echo "error: DMG not found — notarization requires a DMG. Run with --dmg or set CODESIGN_IDENTITY." >&2
        exit 1
    fi

    echo "→ Submitting for notarization…"
    xcrun notarytool submit "$DMG" \
        --apple-id  "$NOTARIZE_APPLE_ID" \
        --password  "$NOTARIZE_PASSWORD" \
        --team-id   "$NOTARIZE_TEAM_ID" \
        --wait

    echo "→ Stapling notarization ticket…"
    xcrun stapler staple "$DMG"
    echo "   ✓ Notarized and stapled"
fi

echo ""
echo "Done."
echo "  App bundle : $APP"
if [[ -f "$DMG" ]]; then echo "  Disk image : $DMG"; fi
