#pragma once
namespace kin {
// A resource remains owned until its conditional restore succeeds or is no longer ours.
inline bool releaseOwned(bool owned, bool stillOwned, bool restoreSucceeded) noexcept {
    if (!owned) return false;
    if (!stillOwned) return false; // external owner changed it; release our bookkeeping
    return restoreSucceeded ? false : true;
}
}
