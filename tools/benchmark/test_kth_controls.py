import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from evaluate_kth_controls import cases
from fetch_kth_controls import extract_member, fetch_range


class Response(io.BytesIO):
    status = 206

    def __init__(self, data, content_range):
        super().__init__(data)
        self.headers = {"Content-Range": content_range}


class KthControlsTests(unittest.TestCase):
    def test_range_server_cannot_ignore_or_shift_requested_interval(self):
        with patch("urllib.request.urlopen", side_effect=lambda *a, **kw: Response(b"abc", "bytes 1-3/9")), patch("time.sleep"):
            with self.assertRaises(ValueError):
                fetch_range("https://example.invalid/data.zip", 0, 2)

    def test_member_checks_crc_after_decompression(self):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("person11_walking_d1_uncomp.avi", b"dataset-video-payload" * 8)
        raw = stream.getvalue()
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            member = archive.infolist()[0]
        def data_range(url, first, last):
            return first, raw[first:last + 1]
        with patch("fetch_kth_controls.fetch_range", side_effect=data_range):
            self.assertEqual(extract_member("unused", member), b"dataset-video-payload" * 8)
            member.CRC ^= 1
            with self.assertRaises(ValueError):
                extract_member("unused", member)

    def fixture(self, root):
        annotations = b"person11_walking_d1 frames 2-5\n"
        (root / "00sequences.txt").write_bytes(annotations)
        video = {"file": "person11_walking_d1_uncomp.avi", "subject": 11, "scenario": 1,
                 "action": "walking", "sha256": "source-hash"}
        dataset = {"annotationsSha256": hashlib.sha256(annotations).hexdigest(), "videos": [video]}
        manifest = root / "manifest.json"
        manifest.write_text(json.dumps(dataset))
        report = {"complete": True, "contract": {"sourceSha256": "source-hash", "modelSha256": "model-hash"},
                  "sourceFps": 25, "durationSeconds": 1,
                  "frames": [{"time": time, "points": [], "detections": 0} for time in [0, .04, .12, .20]]}
        observation = root / (video["file"] + ".poses.json")
        observation.write_text(json.dumps(report))
        return manifest, observation, report

    def test_frame_one_based_half_open_annotations_keep_missing_detections(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, _, _ = self.fixture(root)
            _, groups, coverage, _ = cases(manifest, root)
            window = groups[11][0]
            self.assertEqual(window["evaluationStart"], .04)
            self.assertEqual(window["evaluationEnd"], .20)
            self.assertEqual([f["time"] for f in window["frames"]], [.04, .12])
            self.assertEqual(coverage["frames"], 2)
            self.assertEqual(coverage["detected"], 0)

    def test_invalid_joint_and_source_fingerprints_fail_instead_of_fabricating_pose(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, observation, report = self.fixture(root)
            report["frames"][1]["points"] = [[0, 0, None]] * 17
            observation.write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                cases(manifest, root)
            report["frames"][1]["points"] = []
            report["contract"]["sourceSha256"] = "changed-source"
            observation.write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                cases(manifest, root)


if __name__ == "__main__":
    unittest.main()
