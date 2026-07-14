#!/bin/bash

# Set the ESP32 IP address
ESP_IP="192.168.4.1"

# Upload Test (10 MB from /dev/zero)
echo "Uploading 10 MB to ESP32..."
dd if=/dev/zero bs=1M count=10 2>/dev/null | \
curl -s -w "\nUpload complete: %{speed_upload} bytes/sec\n" -X POST http://$ESP_IP/upload --data-binary @-

# Download Test
echo -e "\nDownloading 10 MB from ESP32..."
curl -s -w "\nDownload complete: %{speed_download} bytes/sec\n" http://$ESP_IP/download -o /dev/null