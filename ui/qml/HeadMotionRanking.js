.pragma library

// Preserve body/static/unknown slots and measured similarity scores. Angular
// extent orders only comparable head-motion rows, never different scopes.
function rerankBestHeadSlots(rows) {
    const result = rows.slice()
    const slots = []
    const measured = []
    for (let i = 0; i < rows.length; ++i) {
        const row = rows[i]
        const extent = row.headMotionExcursionDegrees
        if (row.evidenceScope !== "head" || row.matchType !== "motion"
                || row.manualPair || row.leftRemoved || row.rightRemoved
                || typeof extent !== "number" || !isFinite(extent)
                || extent < 0 || extent > 180) continue
        slots.push(i)
        measured.push({row: row, position: i, extent: extent})
    }
    measured.sort(function(a, b) {
        return b.extent - a.extent || a.position - b.position
    })
    for (let i = 0; i < slots.length; ++i) result[slots[i]] = measured[i].row
    return result
}
