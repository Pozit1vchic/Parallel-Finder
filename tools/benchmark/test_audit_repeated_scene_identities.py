import struct
import tempfile
import unittest
from pathlib import Path

from audit_repeated_scene_identities import cache_frames


def entry(path, key):
    key = key.encode()
    path.write_bytes(struct.pack("<8sIIQ", b"PFCACHE1", 1, len(key), 0) + key)


class PreviewCacheFrameTests(unittest.TestCase):
    def test_sorted_merged_appearances_do_not_reinterpret_legacy_indices(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            entry(cache / "legacy.pfc", "artifact|character-preview-v2-clean-frame|31|0")
            # The visible list may have been sorted after merging: its first
            # row need not be the frame formerly stored under index zero.
            candidates = [{"id": "31", "previews": [{"time": 5.0}]}]
            self.assertEqual(cache_frames(cache, candidates), {})
            self.assertEqual(cache_frames(cache, candidates, assume_legacy_unmerged=True)[5.0][0].name, "legacy.pfc")
            bits = struct.unpack("<Q", struct.pack("<d", 5.0))[0]
            entry(cache / "timed.pfc", f"artifact|character-preview-v3-source-time|{bits}")
            self.assertEqual(cache_frames(cache, candidates)[5.0][0].name, "timed.pfc")


if __name__ == "__main__":
    unittest.main()
