#!/bin/sh
cd "$(dirname "$0")"
chmod +x ./camview 2>/dev/null
export SDL_NOMOUSE=1
./camview nvr.conf > camview.log 2>&1
