.pragma library

function viewForYaw(yaw) {
    if (typeof yaw !== "number" || !isFinite(yaw) || Math.abs(yaw) > 180) return 0
    const sector = Math.floor((yaw + 22.5) / 45)
    return ((sector % 8 + 8) % 8) + 1
}

function bodySupported(d) {
    return !!d && (d.bodyYawEvidence === 2 || d.bodyYawEvidence === 3)
        && viewForYaw(d.bodyYaw) > 0 && d.bodyView === viewForYaw(d.bodyYaw)
        && Number.isInteger(d.bodyYawObservedSamples) && d.bodyYawObservedSamples >= 3
        && Number.isInteger(d.bodyYawRequestedSamples) && d.bodyYawRequestedSamples >= d.bodyYawObservedSamples
}

function headSupported(d) {
    return !!d && viewForYaw(d.headYaw) > 0 && d.headView === viewForYaw(d.headYaw)
        && typeof d.headYawStrength === "number" && isFinite(d.headYawStrength)
        && d.headYawStrength > 0 && d.headYawStrength <= 1
}

function remainingDescriptors(row) {
    const descriptors = []
    for (const side of ["left", "right"]) {
        if (row[side + "Removed"]) continue
        descriptors.push(row[side + "Descriptor"] || {})
    }
    return descriptors
}

// Keep a pair together regardless of which source is on the left. Only
// complete measured evidence participates in the ordered sectors; partial
// and unsupported measurements sort after all supported rows.
function sortKey(row, scope) {
    const descriptors = remainingDescriptors(row)
    if (!descriptors.length) return [1, 0, 0]
    const views = []
    for (const d of descriptors) {
        if (!(scope === "body" ? bodySupported(d) : headSupported(d))) return [1, 0, 0]
        views.push(scope === "body" ? d.bodyView : d.headView)
    }
    views.sort(function(a, b) { return a - b })
    return [0, views[0], views[views.length - 1]]
}

function matches(row, scope, choice) {
    if (!choice) return true
    const views = []
    for (const d of remainingDescriptors(row)) {
        views.push(scope === "body" ? (bodySupported(d) ? d.bodyView : 0)
                                   : (headSupported(d) ? d.headView : 0))
    }
    if (choice === "uncertain") return views.some(function(view) { return view === 0 })
    const wanted = Number(choice)
    return views.length > 0 && wanted >= 1 && wanted <= 8
        && views.every(function(view) { return view === wanted })
}
