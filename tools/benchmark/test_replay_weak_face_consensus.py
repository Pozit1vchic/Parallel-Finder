import unittest

from replay_weak_face_consensus import replay


def observation(time, weak, strong=None):
    return {'time': time, 'weakFace': weak, 'face': strong or []}


class WeakFaceConsensusTests(unittest.TestCase):
    def test_replays_three_consistent_samples_and_leaves_outlier(self):
        items = [observation(i*.2, [1, 0] if i < 3 else [0, 1]) for i in range(4)]
        snapshot = {'tracks': [{'id': 1, 'observations': items}]}
        self.assertEqual(replay(snapshot), [{'track': 1, 'weak': 4, 'promoted': 3}])
        self.assertEqual([bool(item['face']) for item in items], [True, True, True, False])

    def test_strong_contradiction_blocks_promotion(self):
        items = [observation(i*.2, [1, 0], [0, 1] if i == 3 else None) for i in range(4)]
        self.assertEqual(replay({'tracks': [{'id': 1, 'observations': items}]}), [])
        self.assertEqual(items[3]['face'], [0, 1])


if __name__ == '__main__':
    unittest.main()
