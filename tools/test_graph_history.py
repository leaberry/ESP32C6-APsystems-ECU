"""Run the actual optional-history adapter with filesystem fault injection."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    target = Path(tmp) / ('history-test.exe' if os.name == 'nt' else 'history-test')
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra',
                    '-I', str(root), str(root / 'tools/test_graph_history.cpp'),
                    '-o', str(target)], check=True)
    subprocess.run([str(target)], check=True)
print('History retention, polling cadence, CRC, full-flash, failed writes, recovery and disable passed.')
