#!/bin/bash
for p in $(pgrep -f "python3.* .*(gnubox_winshare|gnubox_share|phone_sim|webserver)\.pyw?"); do kill $p; done; pkill Xvfb; ip link del tun9 2>/dev/null; echo stopped
