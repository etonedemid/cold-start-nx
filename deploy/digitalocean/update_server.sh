#!/usr/bin/env bash
# Update the cold_start dedicated server to the latest GitHub release.
# Usage: bash update_server.sh
# Or via one-liner (see README): curl -fsSL <url> | bash
set -euo pipefail

APP_DIR="/opt/cold_start"
SERVICE_NAME="cold_start.service"
REPO="etonedemid/cold-start-nx"
TMP_ZIP="/tmp/cold_start_server_update.zip"

echo "=== COLD START server updater ==="

# Resolve the latest server zip download URL
URL=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
      | grep -o '"browser_download_url":"[^"]*cold_start-linux-server[^"]*\.zip"' \
      | grep -o 'https://[^"]*')

if [ -z "$URL" ]; then
  echo "ERROR: Could not find server release asset." >&2
  exit 1
fi

echo "Downloading ${URL} ..."
curl -fsSL "${URL}" -o "${TMP_ZIP}"

echo "Stopping ${SERVICE_NAME} ..."
sudo systemctl stop "${SERVICE_NAME}"

echo "Installing to ${APP_DIR} ..."
sudo unzip -o "${TMP_ZIP}" -d "${APP_DIR}"
sudo chmod +x "${APP_DIR}/cold_start"
sudo chown -R coldstart:coldstart "${APP_DIR}"

rm -f "${TMP_ZIP}"

echo "Starting ${SERVICE_NAME} ..."
sudo systemctl start "${SERVICE_NAME}"

echo "Done. Server is running."
