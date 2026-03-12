#!/usr/bin/env bash
# Update the cold_start dedicated server to the latest GitHub release.
# Usage: bash update_server.sh
# Or via one-liner (see README): curl -fsSL <url> | bash
set -euo pipefail

APP_DIR="/opt/cold_start"
SERVICE_NAME="cold_start.service"
SERVICE_DEST="/etc/systemd/system/${SERVICE_NAME}"
REPO="etonedemid/cold-start-nx"
TMP_ZIP="/tmp/cold_start_server_update.zip"

echo "=== COLD START server updater ==="

# Ensure required tools are available
for cmd in curl unzip sudo systemctl; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: required command not found: $cmd" >&2
    exit 1
  fi
done

# Resolve the latest server zip download URL
URL=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
      | grep -o '"browser_download_url": *"[^"]*cold_start-linux-server[^"]*\.zip"' \
      | grep -o 'https://[^"]*' || true)

if [ -z "$URL" ]; then
  echo "ERROR: Could not find server release asset." >&2
  exit 1
fi

echo "Downloading ${URL} ..."
curl -fsSL "${URL}" -o "${TMP_ZIP}"

echo "Stopping ${SERVICE_NAME} ..."
sudo systemctl stop "${SERVICE_NAME}" || true

# Ensure the service is restarted even if extraction or any later step fails
_started=0
trap '
  rm -f "${TMP_ZIP}"
  if [ "$_started" -eq 0 ]; then
    echo "Update failed — restarting previous ${SERVICE_NAME} ..." >&2
    sudo systemctl start "${SERVICE_NAME}" || true
  fi
' EXIT

echo "Installing to ${APP_DIR} ..."
sudo unzip -o "${TMP_ZIP}" -d "${APP_DIR}"
sudo chmod +x "${APP_DIR}/cold_start"
sudo chown -R coldstart:coldstart "${APP_DIR}"

# Update the systemd service file if the release includes a newer one
NEW_SERVICE="${APP_DIR}/deploy/digitalocean/${SERVICE_NAME}"
if [ -f "${NEW_SERVICE}" ]; then
  echo "Updating ${SERVICE_DEST} ..."
  sudo cp "${NEW_SERVICE}" "${SERVICE_DEST}"
  sudo systemctl daemon-reload
fi

echo "Starting ${SERVICE_NAME} ..."
_started=1
sudo systemctl start "${SERVICE_NAME}"

echo "Done. Server is running."
