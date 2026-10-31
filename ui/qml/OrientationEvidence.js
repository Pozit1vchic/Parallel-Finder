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
