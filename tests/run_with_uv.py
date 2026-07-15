"""Entry point for uv run: adds tests dir to path and runs pytest."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.resolve()))
import pytest

sys.exit(pytest.main(sys.argv[1:]))