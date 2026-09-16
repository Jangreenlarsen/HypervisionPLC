#!/usr/bin/env bash
# scripts/publish_release.sh — FEAT-169: bygger firmware og publicerer den
# som en GitHub Release, der matcher PROJECT_VERSION (include/constants.h).
# Se docs/RELEASE_PROCEDURE.md for hvornaar og hvordan dette bruges.
#
# Forudsaetninger:
#   - `gh` CLI installeret og logget ind (`gh auth status`)
#   - Alle aendringer for denne version er COMMITTET OG PUSHET foer scriptet
#     koeres — scriptet committer/pusher IKKE kildekode selv (kun den nye
#     git-tag), det er en bevidst adskilt, brugerstyret handling.
#   - Asset-navnet ("firmware.bin") er ren navnekonvention (ikke haandhaevet
#     af nogen kode laengere — FEAT-169s device-side GitHub-OTA, som lod
#     asset-navnet betyde noget, er fjernet i v7.9.68.9). Releases bruges nu
#     udelukkende til manuel download+upload via /ota-siden.
#
# Brug:
#   scripts/publish_release.sh
#   scripts/publish_release.sh --notes "Fritekst release-note"

set -euo pipefail

ENV_NAME="es32d26"
REPO="Jangreenlarsen/Modbus_server_slave_ESP32"
ASSET_NAME="firmware.bin"

cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^#define PROJECT_VERSION *"\([0-9.]*\)".*/\1/p' include/constants.h | head -1)
if [ -z "$VERSION" ]; then
  echo "FEJL: kunne ikke laese PROJECT_VERSION fra include/constants.h" >&2
  exit 1
fi
TAG="v${VERSION}"

NOTES="Se BUGS_INDEX.md for detaljer om denne version."
if [ "${1:-}" = "--notes" ] && [ -n "${2:-}" ]; then
  NOTES="$2"
fi

echo "== Publicerer release ${TAG} for ${REPO} =="

if [ -n "$(git status --porcelain)" ]; then
  echo "FEJL: der er ugemte/ukommiterede aendringer. Committ og push foerst:" >&2
  git status --short
  exit 1
fi

if git rev-parse "$TAG" >/dev/null 2>&1; then
  echo "FEJL: git-tag $TAG findes allerede (lokalt). En release for denne version er formentlig allerede lavet, eller PROJECT_VERSION mangler at blive bumpet." >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "FEJL: 'gh' er ikke logget ind. Koer 'gh auth login' foerst." >&2
  exit 1
fi

echo "-- Bygger (${ENV_NAME}) --"
pio run -e "$ENV_NAME"

BIN_PATH=".pio/build/${ENV_NAME}/firmware.bin"
if [ ! -f "$BIN_PATH" ]; then
  echo "FEJL: $BIN_PATH ikke fundet efter build." >&2
  exit 1
fi

STAGE_DIR=$(mktemp -d)
cp "$BIN_PATH" "${STAGE_DIR}/${ASSET_NAME}"

echo "-- Opretter og pusher git-tag ${TAG} --"
git tag -a "$TAG" -m "Release ${TAG}"
git push origin "$TAG"

echo "-- Opretter GitHub Release --"
gh release create "$TAG" "${STAGE_DIR}/${ASSET_NAME}" \
  --repo "$REPO" \
  --title "$TAG" \
  --notes "$NOTES"

rm -rf "$STAGE_DIR"
echo "== Faerdig: ${TAG} publiceret med asset ${ASSET_NAME} =="
echo "Enheder kan nu finde denne version via 'Tjek for opdatering' paa /system-siden."
