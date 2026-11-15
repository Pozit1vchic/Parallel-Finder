.pragma library

// Preserve body/static/unknown slots and measured similarity scores. Angular
// extent orders only comparable head-motion rows, never different scopes.
function rerankBestHeadSlots(rows, groupKey) {
    const result = rows.slice()
    const groups = new Map()
    for (let i = 0; i < rows.length; ++i) {
        const row = rows[i]
        const extent = row.headMotionExcursionDegrees
        if (row.evidenceScope !== "head" || row.matchType !== "motion"
                || row.manualPair || row.leftRemoved || row.rightRemoved || row.classificationConflict
                || typeof extent !== "number" || !isFinite(extent)
                || extent < 0 || extent > 180) continue
        const key = groupKey ? groupKey(row) : ""
        if (!groups.has(key)) groups.set(key, [])
        groups.get(key).push({row: row, position: i, extent: extent})
    }
    for (const measured of groups.values()) {
        const slots = measured.map(entry => entry.position)
        measured.sort((a, b) => b.extent - a.extent || a.position - b.position)
        for (let i = 0; i < slots.length; ++i) result[slots[i]] = measured[i].row
    }
    return result
}
