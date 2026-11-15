# Auction visibility — Phase 4 PR A

Owner: CosmicCuddle. Maintenance. Coordination #62, accepted amendments #56 (6893304145).
Base: Core master 21ff1973bc75216f8a0d0f3d7731eb3e8894ff97. Isolated development only.

## Contract and custom modification register entry
Feature: generic auction browse visibility value contract, owned by Core.
Paths: src/server/game/AuctionHouse/AuctionVisibility.h; tests/auction_visibility/test_auction_visibility.cpp; this note.
Source dependency: C++20 standard library only; no module, Player, Item, database, script or DBC dependencies.
Status: standalone tested, not wired into searcher or live. No configuration, schema, packet, pricing, auctionability, transaction or owner/bidder changes.

Policy carries nonzero generation/domain, maximum ordered rank and centrally allocated capability mask. Viewer values are copied on the owning thread; allocation table is trusted Core-owned configuration (not provider-supplied). BuildViewer accepts one ordered authority, disjoint bit ownership and unique contributions. Invalid provider contributes nothing; malformed ownership/domain claims deny the viewer. Real allocation IDs/bits and registration are deferred; fixture values are synthetic, not IP or AHBot identifiers.
Tag is explicitly Unknown, Unrestricted or Controlled. Unrestricted is canonical zero requirement with current generation/domain; no missing tag can become unrestricted. Combine uses maximum minimum and OR requirements; invalid/unknown absorbs. A controlled baseline rank zero remains controlled, not unrestricted.
Disabled bypasses predicate; Observe preserves visibility and reports wouldPermit/reason; Enforced denies invalid/unknown/stale values. Invalid mode denies. Predicate is browse-only; no transaction authority. Explicit current unrestricted permits even when viewer unavailable. We deliberately require current generation even for unrestricted tags, to prevent stale unrestricted classification widening access.
PR B must fence actual response emission against active generation (matching old viewer+old tag+old request context can still pass this pure predicate); immutable publication, mutation replay, and all-worker readiness remain unimplemented. Real provider registration/identity, policy allocation lifetime, actual search/default/getAll and bid checks, View-As, owner/bidder lists and configuration are later integration work. No live filter activation.

## Reproduction
From Core repository root:
```sh
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/server/game/AuctionHouse tests/auction_visibility/test_auction_visibility.cpp -o /tmp/ahvis-a-gcc && /tmp/ahvis-a-gcc
clang++ -std=c++20 -Wall -Wextra -Werror -Isrc/server/game/AuctionHouse tests/auction_visibility/test_auction_visibility.cpp -o /tmp/ahvis-a-clang && /tmp/ahvis-a-clang
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc/server/game/AuctionHouse tests/auction_visibility/test_auction_visibility.cpp -o /tmp/ahvis-a-san && /tmp/ahvis-a-san
```
Executed equivalent include paths in disposable /tmp/ahvis62 using exact source blobs header 89a344af5deca5a24914943ce25e4cecbc827659 and fixture 233f7022fb3d64ba37ac73540bdabd43a5c348ba. GCC 14.2, Clang 17 and ASan/UBSan exit 0. 41 compile-time assertions, synthetic 120-entry browse oracle (80 permitted/40 denied, Observe all, Disabled all, pages 50+30). Not actual search packet/getAll, native Core build, real provider or gameplay tests. No hosted run dispatched.

## Next gates and rollback
Information reviews interface and agrees real ownership allocation before B/C. B covers actual retag/rebuild, mutation replay, response fence and concurrency; later search/count/pagination, transaction authorization, owner/bidder regression, no-IP/absent-provider parity, composition/native and gameplay. No real item classification or installed configuration changes. Rollback: discard/revert isolated PR A; no migration or runtime effect. Preserve current custom release priorities and all existing changes.
