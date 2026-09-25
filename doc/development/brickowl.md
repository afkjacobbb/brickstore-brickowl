# Native BrickOwl prototype

This change extends the existing C++/Qt BrickStore desktop application. It adds no web UI,
server, external database, JavaScript runtime or .NET dependency. The abandoned earlier
web prototype is not part of this patch.

## Source review and implementation plan

Base BrickStore revision: `2086b7f108d27433330a8ecddc2d50786f5cd4b8`.
BrickOwlSharp reference revision: `8bdbc1b05845ad1d2d64ecc70f319094957bf254`.

Reviewed existing components:

- `src/bricklink/core.*`, `store.*`, `io.*`: authenticated transfers, store download,
  catalogue lookup, BrickLink XML import/export. Store::startUpdate downloads XML through
  Core's authenticated TransferJob using the BrickStore access token. It does not use
  the four-credential OAuth Store API. Existing exports produce mass-upload/update XML.
- `src/utility/transfer.*`: QNetworkAccessManager on a worker thread; logging includes
  URLs. BrickOwl GET authentication puts the key in the query, so the new client uses
  the existing alternative pattern (QNAM + QCoro, as in `common/announcements.cpp`) to
  avoid sending secret-bearing URLs through Transfer logging.
- `src/common/config.*`, `utility/credentialsmanager.*`: existing credentials service.
  The new keys use this same platform mechanism (Windows Credential Manager, Apple
  Keychain, libsecret on supported Linux builds, upstream's obfuscated fallback otherwise).
- `src/desktop/settingsdialog.*`: existing stacked settings pages, native form widgets.
- `src/common/application.cpp`, `documentio.*`, `documentmodel.*`, `document.*`:
  store-import action, document creation, native Lot model, undoable lot changes and BSX.
- `src/bricklink/lot.*`: identity, condition, quantity, copying, equality and binary save.
- `src/common/actionmanager.*`, `desktop/mainwindow.*`: action registration and menus.
- BrickOwlSharp `BrickOwlClient.cs`, `Inventory.cs`, `NewInventory.cs`,
  `UpdateInventory.cs`, `DeleteInventory.cs`, `CatalogItem*.cs`, `Color.cs`,
  `Condition.cs`, `IdType.cs`, `NewInventoryResult.cs`: wire protocol reference.

Plan executed: preserve existing BrickLink flows; add two narrow native API clients;
add settings/actions; convert BrickOwl data into existing lots; extend lot persistence;
provide a native import/preview/apply dialog. No generic marketplace framework.

## UI

- Settings → **BrickOwl**, directly below BrickLink: masked API key and connection test.
  OK saves credentials. The connection test reads inventory; it does not modify it.
- Settings → **BrickLink** → **BrickLink Store API — manual synchronization**:
  consumer key, consumer secret, token and token secret. These are separate from the
  existing BrickStore access token, which remains unchanged.
- File → Import → **BrickOwl Store Inventory…**.
- File → Export → **Synchronize BrickLink ↔ BrickOwl…** (requires an open document).

The desktop feature uses native Qt widgets and the existing style/translations mechanism.
Mobile menus are not extended in this prototype. New source strings are English;
translation catalog updates remain to be done.

## Authentication and requests

BrickOwl: `https://api.brickowl.com/v1/`, GET `key` query parameter, POST URL-encoded form
`key` field. Requests have a 20-second timeout, no automatic redirects and no raw response,
request URL or credential logging. Network/API errors are reduced to safe messages.

BrickLink Store API: `https://api.bricklink.com/api/store/v1/`, OAuth 1.0 HMAC-SHA1 header.
Register your public IP in BrickLink's Store API configuration. The application does not
assume the existing BrickStore access token authorizes this API.

Implemented BrickOwl client operations:

| Operation | Request |
|---|---|
| Test authentication / inventory | GET `inventory/list` |
| One lot before update | GET `inventory/list?lot_id=…` |
| Catalogue details | GET `catalog/lookup?boid=…` |
| Exact external identifier lookup | GET `catalog/id_lookup?id=…&type=Part&id_type=bl_item_no` |
| Color references | GET `catalog/color_list` |
| Create a lot | POST `inventory/create` |
| Quantity change | POST `inventory/update`, `relative_quantity` |
| Delete a lot | POST `inventory/delete` (client method; not exposed as a destructive UI action) |

BrickLink client implements GET `inventories`, GET `inventories/{id}` and PUT
`inventories/{id}` with a signed quantity delta. Existing BrickLink functionality is not
rewritten. Existing BO lots only have quantity changed; price/condition/notes editing is
not part of this first sync workflow. Import preserves price, notes and condition.

## Inventory and mapping

The inventory remains `BrickLink::Lot` inside the normal `DocumentModel`.

- `lotId()` retains its original BrickLink meaning.
- Separate fields store BO lot ID, BOID, BO color ID and exact BO condition grade.
- BSX and DifferenceBaseValues preserve these fields. The schema is extended accordingly.
- Binary clipboard/autosave lot format is version 6; the reader accepts old version 5 too.
  Older unmodified builds cannot read the new binary format. BSX is the interchange format;
  older builds may discard the additional BO tags when saving.
- Copy and equality include the added metadata; regular undo/document saving remains used.
- Item mapping requires exactly one `bl_item_no` reference from the BO catalogue plus a
  supported item type and a known BrickStore catalogue entry. No name matching.
- Color mapping uses `catalog/color_list` → `bl_ids`. Ambiguous references are rejected.
  IDs in the two color namespaces are never compared directly.
- Full BO condition grades are retained even when several map to BrickLink Used.
- Existing lot identity is matched first when known; otherwise exact product/color/condition
  is used only when unique. Duplicate matches and duplicate document mappings are excluded.
- New BO lot creation is deliberately limited to uniquely mapped **new parts**. Unsupported
  or unrecognized catalogue entries remain reported, rather than guessed.
- For import, enter the actual BO store currency explicitly. There is no currency conversion.

## Manual synchronization

1. Import the BrickLink inventory using the existing workflow, or open an existing BSX.
2. Open synchronization and explicitly choose Document, BrickLink or BrickOwl as quantity
   source. There is no implicit minimum, summing or assumption that one store is always right.
3. Download both inventories and inspect selected rows, current quantities and targets.
4. Optionally allow creation of missing BO lots. Unmapped remote data blocks creation so
   an unknown existing lot cannot silently be duplicated.
5. Pause sales on both stores, check the confirmation, then apply checked rows.
6. Both inventories and the document are rechecked before the first mutation. Each store
   also gets a fresh quantity check immediately before its update. Delta writes avoid
   blindly restoring units sold concurrently; cross-store atomicity is not available.
7. After successful writes, re-read both inventories and label each row verified or conflict.
8. Save the document to retain BO lot IDs and the confirmed quantities.

Writes are sequential and never automatically retried. If the second store fails, the first
may already be changed: processing stops, reports partial completion and requires a fresh
preview. Undo affects the local document only; it cannot undo remote writes.

Before creation, a minimal pending-operation marker is synced in the existing QSettings.
An uncertain create response leaves it in place, preventing duplicate POST replay. It holds
only a hash and boolean, not inventory or credentials. Inspect BO and refresh to adopt an
existing lot. If no lot was created, an advanced user must verify that fact before removing
that one `BrickOwl/UnconfirmedCreate/…` marker from settings. There is no automatic retry.

## Verification and limitations

See `VALIDATION.md` for actual results from this implementation session.

No real account credentials were supplied. Live authentication, rate limits, large-inventory
behavior, zero-quantity listing retention and write responses still need account testing.
BrickOwl's official documentation URL returned HTTP 403 during this task. The client is
based on the user-provided BrickOwlSharp source, not claimed to be certified against current
live API behavior. BrickLink's current documentation page did not expose useful body text;
the official static Store API reference was readable. Before production use, verify both
contracts with the live accounts and a small, paused-store test inventory.

The prototype stops safely on unsupported payloads. It does not implement periodic polling,
webhooks, order deduplication, sale/cancellation processing, automatic restocking, multi-account
inventory separation, bulk rate-limit scheduling, or automatic conflict resolution.

The next stage should reuse these clients and lot IDs, add an order-event cursor/ledger
inside BrickStore's existing persistence, deduplicate order items and apply deltas with
explicit cancellation/edit handling. Comparing two snapshots alone is insufficient to know
whether a difference means a sale, a manual correction or a new stock delivery.

## Build

Use BrickStore's normal CMake/Qt desktop build instructions. The patch adds no runtime
libraries beyond the existing Qt/QCoro dependencies. Example for a configured Qt environment:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

Focused Linux fixture tests (with QCoro 0.12 installed to a CMake prefix):

```sh
cmake -S tests/brickowl -B build-brickowl-tests -G Ninja -DCMAKE_PREFIX_PATH=/path/to/qcoro/prefix
cmake --build build-brickowl-tests
ctest --test-dir build-brickowl-tests --output-on-failure
```

## Sources

- https://github.com/rgriebl/brickstore
- https://github.com/stephanstapel/BrickOwlSharp
- https://www.brickowl.com/api_docs (403 when checked)
- https://www.bricklink.com/v3/api.page
- https://static.bricklink.com/alpha/default/api_wiki.html

BrickOwlSharp was consulted as a protocol reference, not added as a dependency or copied
as C# source. This extension follows BrickStore's GPL-3.0-only licensing.
