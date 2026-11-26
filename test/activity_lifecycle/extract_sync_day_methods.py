"""Compile Sync Day's automatic Wi-Fi flow against host-side radio and clock stand-ins."""

from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
Path(sys.argv[2]).write_text(re.sub(r"^#include .+\n", "", source, flags=re.MULTILINE), encoding="utf-8")
