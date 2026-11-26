"""Compile Sync Day's automatic Wi-Fi flow against host-side radio and clock stand-ins."""

from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
timeout, = re.findall(r"^constexpr unsigned long WIFI_JOIN_TIMEOUT_MS = \d+;$", source, re.MULTILINE)
methods = ("startAutoSync", "finishAutoWifi", "handleCustomInput", "syncTime")
bodies = [
    re.search(rf"^(?:void|bool) SyncDayActivity::{name}\([^\n]*\) \{{\n.*?^\}}", source, re.MULTILINE | re.DOTALL)[0]
    for name in methods
]
Path(sys.argv[2]).write_text("\n\n".join([timeout, *bodies]) + "\n", encoding="utf-8")
