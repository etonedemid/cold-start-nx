#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/opt/cold_start"
SERVICE_NAME="cold_start.service"

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  ca-certificates \
  curl \
  unzip \
  libenet7 libminiupnpc17

if ! id -u coldstart >/dev/null 2>&1; then
  sudo useradd --system --home-dir "$APP_DIR" --shell /usr/sbin/nologin coldstart
fi

sudo mkdir -p "$APP_DIR"
sudo chown -R coldstart:coldstart "$APP_DIR"

echo "Copy your built Linux files to $APP_DIR:"
echo "  cold_start"
echo "  romfs/"
echo "Then install service with:"
echo "  sudo cp deploy/digitalocean/$SERVICE_NAME /etc/systemd/system/$SERVICE_NAME"
echo "  sudo systemctl daemon-reload"
echo "  sudo systemctl enable --now $SERVICE_NAME"
echo ""
echo "Open firewall UDP 7777:"
echo "  sudo ufw allow 7777/udp"
