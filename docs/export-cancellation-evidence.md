# Export cancellation: phase-controlled verification

During the body-direction integration regression run, the unrelated real-encoder
cancellation check failed only its `encoderStarted` assertion:
`build/body-direction-replay-full-suite.log`, 13/14 targets passed. The cut was
cancelled and its destination/temporary-output cleanup assertions passed. The
watcher waited five seconds for a nonzero temporary-file size before requesting
stop, so this failure did not establish cancellation during active encoding or
an export-cleanup defect. The exact reason the size predicate was not observed
in that run is not established. The failed log is retained.

The verification now separates two requirements, without changing CutService:

- `CancellationAfterEncodedProgressCleansUnpublishedOutput` uses an actual
  FFmpeg encoder and a 60-second real media fixture. It requests cancellation
  after positive encoded time strictly below the clip duration, observes the
  temporary file, and requires cancellation with no published/temporary output.
  A finite 30-second failure guard prevents an unresponsive encoder from hanging
  the test. End-only progress does not satisfy the test. The callback avoids a
  race between startup, filesystem metadata/AVIO buffering and an arbitrary
  cancellation deadline. It proves cancellation after media processing begins
  and before publication; progress delivery alone does not prove the child was
  still alive at that precise callback.
- `CancellationStopsAnActiveEncoderAndCleansTemporaryOutput` separately uses
  the existing controlled FFmpeg child. The child emits an initial progress
  record and then remains alive for 30 seconds. The test requests cancellation
  on that record and requires a cancelled result, temporary-file cleanup and
  return in less than three seconds. This verifies stopping a live child
  independently of real-encoder speed and buffered file size.

Both controls pass three consecutive executions in
`build/export-cancel-barriers-verified.log`. The preceding real-progress-only
control also passed three runs (`export-cancel-progress-verified.log`). The
existing preparation-time cancellation and stalled-encoder controls remain
separate. The intermediate full suite passes 14/14 in 124.78 seconds
(`body-direction-replay-full-suite-verified.log`); that run precedes adding the
separate live-child control. Final current-tree verification is recorded in the
completion audit: all 14 targets pass in 115.39 seconds
(`build/body-direction-nvdec-final-suite.log`), including both separate
cancellation controls. These are test-harness improvements, not a
claim that an observed production export bug was fixed.
