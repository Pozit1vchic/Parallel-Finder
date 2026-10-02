-- Read-only Nsight Systems SQLite query. Merge overlapping CUDA kernel,
-- memcpy and memset intervals instead of summing potentially overlapping time.
-- The denominator is first-to-last captured CUDA work, not analysis time or
-- SM occupancy. NVDEC's separate video engine is not represented here; use a
-- single-target process trace rather than a system-wide capture.
WITH events AS (
    SELECT deviceId, start, end FROM CUPTI_ACTIVITY_KIND_KERNEL
    UNION ALL
    SELECT deviceId, start, end FROM CUPTI_ACTIVITY_KIND_MEMCPY
    UNION ALL
    SELECT deviceId, start, end FROM CUPTI_ACTIVITY_KIND_MEMSET
), ordered AS (
    SELECT deviceId, start, end,
        MAX(end) OVER (
            PARTITION BY deviceId ORDER BY start, end
            ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING
        ) AS previousEnd
    FROM events
), summary AS (
    SELECT deviceId, COUNT(*) AS eventCount,
        MAX(end) - MIN(start) AS spanNs,
        SUM(MAX(0, end - MAX(start, COALESCE(previousEnd, start)))) AS activeNs,
        SUM(CASE WHEN previousEnd IS NOT NULL AND start - previousEnd > 1000000
            THEN 1 ELSE 0 END) AS gapsOver1ms,
        MAX(MAX(0, start - COALESCE(previousEnd, start))) AS longestGapNs
    FROM ordered GROUP BY deviceId
)
SELECT deviceId, eventCount, ROUND(spanNs / 1000000000.0, 6) AS spanSeconds,
    ROUND(activeNs / 1000000000.0, 6) AS capturedActiveSeconds,
    ROUND(100.0 * activeNs / NULLIF(spanNs, 0), 2) AS capturedCoveragePercent,
    gapsOver1ms, ROUND(longestGapNs / 1000000.0, 3) AS longestGapMs
FROM summary;
