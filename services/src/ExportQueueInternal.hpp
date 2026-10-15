#pragma once
#include "ExportConcurrency.hpp"
#include <pfservices/ExportQueue.hpp>

namespace pfservices::detail {
// Internal executor seam for deterministic scheduler tests. Public exports use
// the same scheduler with CutService and the adaptive CPU/memory budget.
using CutExecutor = std::function<CutResult(const CutRequest&)>;
ExportBatchResult runExportQueueWithExecutor(const std::vector<CutRequest>& jobs,
    ExportBudget budget, CutExecutor execute, std::stop_token stop = {},
    ExportProgress progress = {});
}
