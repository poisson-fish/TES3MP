import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VNEXT_DOCS = ROOT / "docs" / "vnext"
ACTIVE_DOCS = {
    "README.md",
    "CURRENT.md",
    "DEVELOPMENT.md",
    "DECISIONS.md",
    "PLAN.md",
}
SUPPORT_DOCS = {
    "proofs/cares/README.md",
    "proofs/flatbuffers/README.md",
    "proofs/flatbuffers/ANDROID_ARM64.md",
    "proofs/gamenetworkingsockets/README.md",
    "proofs/gamenetworkingsockets/ANDROID_ARM64.md",
    "proofs/gamenetworkingsockets/TRUST_INTEGRATION_ASSESSMENT.md",
}
MAX_ACTIVE_WORDS = 5_000
DOCUMENT_WORD_LIMITS = {"CURRENT.md": 850, "PLAN.md": 1_500}
LOCAL_LINK = re.compile(r"\[[^]]+\]\((?!https?://|#)([^)#]+)(?:#[^)]+)?\)")


class VnextDocumentationTests(unittest.TestCase):
    def test_active_document_set_stays_small_and_unambiguous(self):
        actual = {path.relative_to(VNEXT_DOCS).as_posix() for path in VNEXT_DOCS.rglob("*.md")}
        self.assertEqual(ACTIVE_DOCS | SUPPORT_DOCS, actual)

        words = 0
        for name in ACTIVE_DOCS:
            count = len((VNEXT_DOCS / name).read_text(encoding="utf-8").split())
            words += count
            if name in DOCUMENT_WORD_LIMITS:
                self.assertLessEqual(count, DOCUMENT_WORD_LIMITS[name], name)
        self.assertLessEqual(words, MAX_ACTIVE_WORDS)

    def test_active_local_links_resolve(self):
        documents = [ROOT / "README.md", ROOT / "AGENTS.md"]
        documents.extend(VNEXT_DOCS / name for name in ACTIVE_DOCS)

        missing = []
        for document in documents:
            text = document.read_text(encoding="utf-8")
            for match in LOCAL_LINK.finditer(text):
                target = (document.parent / match.group(1)).resolve()
                if not target.exists():
                    missing.append(f"{document.relative_to(ROOT)} -> {match.group(1)}")
        self.assertEqual([], missing)


if __name__ == "__main__":
    unittest.main()
