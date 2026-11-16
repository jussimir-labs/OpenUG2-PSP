#ifndef NFSU2_MEMORY_PROFILE_H
#define NFSU2_MEMORY_PROFILE_H
#include <stdint.h>

/* Capacity policy, not a physical model/partition-size claim. The budget is
 * the largest successful boot malloc, a historical lower bound. arena/used/
 * free are a separate post-free snapshot (free may trim the arena). USER
 * free/max are outside libc's reserved heap and never added to the budget. */
enum { MEMORY_AUTO_COMPACT, MEMORY_AUTO_EXTENDED };
enum { MEMORY_UNKNOWN, MEMORY_INVALID, MEMORY_NORMAL_BUDGET,
       MEMORY_RESERVE_UNMET, MEMORY_EXTRA_BUDGET };
typedef struct {
    uint32_t valid,arena,used,free,boot_largest,user_free,user_max;
} MemoryObservation;
typedef struct {
    uint32_t essential_floor,transition_reserve,cache_cap,extended_min;
} MemoryPolicy;
typedef struct {
    uint32_t kind,reason,heap_budget,essential_floor,transition_reserve;
    uint32_t world_cache_bytes,compact_features;
} MemoryProfile;

/* Policy allowances, not measured peaks: keep a normal-user-sized floor and
 * 8 MiB for overlapping transitions before optional retention. World's
 * resident ledger excludes metadata/allocator overhead; its resulting cap
 * is deliberately below this allowance, not an independent heap quota. */
MemoryPolicy memory_profile_default_policy(void);
MemoryProfile memory_profile_select(const MemoryObservation *observed,
                                    const MemoryPolicy *policy);
uint32_t memory_profile_optional_budget(uint32_t budget,uint32_t essential,
                                       uint32_t reserve,uint32_t cap);
/* Pressure only reduces optional retention; it does not change features. */
uint32_t memory_profile_cache_after_failure(uint32_t current,uint32_t resident,uint32_t step);
/* Optional-load admission only, not an allocator reservation. requested and
 * resident include conservative ownership bounds; concurrent owners may still
 * make malloc fail, so callers must retain rollback and retry. */
enum { MEMORY_ADMIT, MEMORY_DEFER_UNKNOWN, MEMORY_DEFER_CAP, MEMORY_DEFER_RESERVE };
unsigned memory_profile_admit_optional(uint32_t heap_budget,uint32_t used,
    uint32_t reserve,uint32_t cap,uint32_t resident,uint32_t requested);
#endif
