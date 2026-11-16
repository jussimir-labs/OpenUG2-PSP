#include "memory_profile.h"

MemoryPolicy memory_profile_default_policy(void) {
    return (MemoryPolicy){24u<<20,8u<<20,24u<<20,32u<<20};
}
uint32_t memory_profile_optional_budget(uint32_t budget,uint32_t essential,
                                       uint32_t reserve,uint32_t cap) {
    if(essential>budget)return 0;
    uint32_t left=budget-essential;
    if(reserve>left)return 0;
    left-=reserve;return left<cap?left:cap;
}
uint32_t memory_profile_cache_after_failure(uint32_t current,uint32_t resident,uint32_t step) {
    uint32_t held=current<resident?current:resident;
    return step<held?held-step:0;
}
unsigned memory_profile_admit_optional(uint32_t heap_budget,uint32_t used,
    uint32_t reserve,uint32_t cap,uint32_t resident,uint32_t requested) {
    if(!heap_budget || !reserve || !requested || used>heap_budget)return MEMORY_DEFER_UNKNOWN;
    if(resident>cap || requested>cap-resident)return MEMORY_DEFER_CAP;
    uint32_t available=heap_budget-used;
    if(reserve>available || requested>available-reserve)return MEMORY_DEFER_RESERVE;
    return MEMORY_ADMIT;
}
MemoryProfile memory_profile_select(const MemoryObservation *o,const MemoryPolicy *p) {
    MemoryProfile r={.kind=MEMORY_AUTO_COMPACT,.reason=MEMORY_UNKNOWN,.compact_features=1};
    if(!o || !p || !o->valid)return r;
    if(o->valid!=1 || !o->boot_largest || o->used>o->arena ||
       o->free!=o->arena-o->used || o->user_max>o->user_free ||
       !p->extended_min || !p->essential_floor || !p->transition_reserve) {
        r.reason=MEMORY_INVALID;return r;
    }
    /* free() may trim the arena after the successful boot allocation. Its
     * historical lower bound and the post-free snapshot have distinct epochs.
     * Do not compare that allocation to post-trim free or infer capacity from
     * post-trim arena. Excluding already-live bytes is conservative here. */
    r.heap_budget=o->boot_largest;
    r.essential_floor=o->used>p->essential_floor?o->used:p->essential_floor;
    r.transition_reserve=p->transition_reserve;
    /* Preserve the existing feature decision as a separate legacy policy.
     * It is never inferred from a nonzero cache cap or the physical model. */
    r.compact_features=o->boot_largest<(32u<<20);
    if(r.heap_budget<p->extended_min){r.reason=MEMORY_NORMAL_BUDGET;return r;}
    r.kind=MEMORY_AUTO_EXTENDED;
    r.world_cache_bytes=memory_profile_optional_budget(r.heap_budget,r.essential_floor,
                                                      r.transition_reserve,p->cache_cap);
    r.reason=r.world_cache_bytes?MEMORY_EXTRA_BUDGET:MEMORY_RESERVE_UNMET;
    return r;
}
