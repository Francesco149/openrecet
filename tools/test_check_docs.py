from __future__ import annotations

import importlib.util
import sys
import tempfile
from pathlib import Path


MODULE_PATH = Path(__file__).parent / "ci" / "check_docs.py"
SPEC = importlib.util.spec_from_file_location("check_docs", MODULE_PATH)
assert SPEC and SPEC.loader
check_docs = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = check_docs
SPEC.loader.exec_module(check_docs)


def _write(root: Path, rel: str, text: str) -> Path:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def test_valid_local_anchor_and_external_links(tmp_path: Path) -> None:
    _write(tmp_path, "target.md", "# Target\n")
    source = _write(
        tmp_path,
        "docs/source.md",
        "[target](../target.md#target) [self](#section) "
        "[external](https://example.com/x)\n",
    )
    problems, count = check_docs.check_document(source, tmp_path)
    assert problems == []
    assert count == 1


def test_broken_and_escaping_links_fail(tmp_path: Path) -> None:
    source = _write(
        tmp_path,
        "docs/source.md",
        "[missing](missing.md)\n[escape](../../outside.md)\n",
    )
    problems, count = check_docs.check_document(source, tmp_path)
    assert count == 2
    assert any("broken local link" in p.message for p in problems)
    assert any("escapes repository" in p.message for p in problems)


def test_private_memory_and_model_names_fail(tmp_path: Path) -> None:
    source = _write(tmp_path, "source.md", "[[private_key]]\nDelegate to Sonnet.\n")
    problems, _ = check_docs.check_document(source, tmp_path)
    messages = [p.message for p in problems]
    assert any("auto-memory" in m for m in messages)
    assert any("R3/R2/R1" in m for m in messages)


def test_missing_authoritative_file_fails(tmp_path: Path) -> None:
    problems, count = check_docs.check_document(tmp_path / "missing.md", tmp_path)
    assert count == 0
    assert len(problems) == 1
    assert "missing" in problems[0].message


def main() -> int:
    tests = (
        test_valid_local_anchor_and_external_links,
        test_broken_and_escaping_links_fail,
        test_private_memory_and_model_names_fail,
        test_missing_authoritative_file_fails,
    )
    for test in tests:
        with tempfile.TemporaryDirectory() as td:
            test(Path(td))
        print(f"PASS {test.__name__}")
    print(f"OK: check_docs ({len(tests)} tests)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
