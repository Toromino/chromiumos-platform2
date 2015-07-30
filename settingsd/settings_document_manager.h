// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SETTINGSD_SETTINGS_DOCUMENT_MANAGER_H_
#define SETTINGSD_SETTINGS_DOCUMENT_MANAGER_H_

#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/observer_list.h>

#include "settingsd/blob_ref.h"
#include "settingsd/blob_store.h"
#include "settingsd/settings_blob_parser.h"
#include "settingsd/settings_service.h"
#include "settingsd/source.h"

namespace base {
class Value;
}  // namespace base

namespace settingsd {

class SettingsDocument;
class SettingsMap;

// SettingsDocumentManager maintains the set of currently installed settings
// documents and makes sure they are valid at all times. It checks document
// validity against the trust configuration, which is maintained (along with all
// other setting values in a SettingsMap).
//
// Whenever there is a change to trust configuration due to settings document
// insertion or removal in the SettingsMap, this SettingsDocumentManager
// resolves implications for existing settings documents. For example, inserting
// a document which changes a configuration source may invalidate existing
// settings documents generated by the affected source. Thus, settings documents
// depending on the modified source need to be re-validated. They'll get removed
// if validation fails in the updated trust configuration.
class SettingsDocumentManager : public SettingsService {
 public:
  // Indicates the result of a settings document insertion operation.
  enum InsertionStatus {
    kInsertionStatusSuccess,          // Document inserted successfully.
    kInsertionStatusVersionClash,     // Source version already used.
    kInsertionStatusCollision,        // Collision with other document.
    kInsertionStatusAccessViolation,  // Document touches off-bounds keys.
    kInsertionStatusParseError,       // Failed to parse the blob.
    kInsertionStatusValidationError,  // Blob failed validation.
    kInsertionStatusBadPayload,       // Failed to decode blob payload.
    kInsertionStatusStorageFailure,   // Failed to write the blob to BlobStore.
    kInsertionStatusUnknownSource,    // Blob origin unknown.
  };

  // Constructs a new instance. The initial trust configuration must  be passed
  // in via |trusted_document|, which should be retrieved from a trusted source,
  // such as a file from the OS image, protected by verified boot.
  // |trusted_document| is typically used to allow the OS image to hard-code
  // values for certain settings and to set up additional trusted configuration
  // sources.
  //
  // WARNING: NEVER CALL THIS CONSTRUCTOR WITH A DOCUMENT RECEIVED FROM THE
  // USER, NETWORK, UNTRUSTED STORAGE ETC. OR YOU WILL LOSE ALL END-TO-END
  // SETTINGS AUTHENTICATION AFFORDED BY SETTINGSD.
  SettingsDocumentManager(
      const SettingsBlobParserFunction& settings_blob_parser_function,
      const SourceDelegateFactoryFunction& source_delegate_factory_function,
      const std::string& storage_path,
      std::unique_ptr<SettingsMap> settings_map,
      std::unique_ptr<const SettingsDocument> trusted_document);
  ~SettingsDocumentManager();

  // Initializes the SettingsDocumentManager by inserting the trusted document
  // into the SettingsMap and loading settings blobs for all known sources from
  // disk.
  void Init();

  // SettingsService:
  const base::Value* GetValue(const Key& key) const override;
  const std::set<Key> GetKeys(const Key& prefix) const override;
  void AddSettingsObserver(SettingsObserver* observer) override;
  void RemoveSettingsObserver(SettingsObserver* observer) override;

  // Decodes a binary settings blob and inserts the included settings document
  // into the configuration. This runs the full set of validation against the
  // settings blob, i.e. the blob gets validated against the source delegate
  // (signature check etc.), it needs to have a valid non-conflicting version
  // stamp, the source must have access to settings keys the document touches,
  // etc.
  //
  // The return value indicates whether insertion was successful or hit an
  // error. No settings changes will occur it the return value is not
  // kInsertionStatusSuccess.
  InsertionStatus InsertBlob(const std::string& source_id, BlobRef blob);

 private:
  friend class SettingsDocumentManagerTest;

  // Keeps track of all documents and their corresponding BlobStore handles for
  // a source.
  struct DocumentEntry {
    DocumentEntry(std::unique_ptr<const SettingsDocument> document,
                  BlobStore::Handle handle);

    // A SettingsDocument.
    std::unique_ptr<const SettingsDocument> document_;

    // The BlobStore handle that the Blob the above document was parsed from can
    // be retrieved with.
    BlobStore::Handle handle_;
  };

  // Keeps track of all known sources and their associated document entries.
  struct SourceMapEntry {
    explicit SourceMapEntry(const std::string& source_id);
    ~SourceMapEntry();

    // The current source configuration. This reflects the configuration
    // specified in |settings_map_|. When the latter changes, the affected
    // sources will get re-parsed.
    Source source_;

    // All documents owned by the source and their respective handles, sorted
    // according to |source_|'s version component in the document's version
    // stamp.
    std::vector<DocumentEntry> document_entries_;
  };

  // Install a new settings document. The |document| is assumed to be fully
  // validated against the source identified with |source_id|. Inserts the
  // document into |settings_map_|, handles any trust configuration changes and
  // notifies observers.
  //
  // The return value indicates whether the insertion succeeded or ran into an
  // error. For return values other than kInsertionStatusSuccess no settings
  // will be changed.
  InsertionStatus InsertDocument(
      std::unique_ptr<const SettingsDocument> document,
      BlobStore::Handle,
      const std::string& source_id);

  // Finds the DocumentEntry, deletes the Blob associated with the entry's
  // handle in BlobStore and deletes the DocumentEntry from the source map.
  // Returns true on success. Otherwise, returns false.
  bool PurgeBlobAndDocumentEntry(const SettingsDocument* document);

  // Attempts to parse and validate a settings blob. On success, returns success
  // status and populates |container| with the parsed and validated
  // LockedSettingsContainer. On error, returns a status code indicating the
  // failure mode.
  SettingsDocumentManager::InsertionStatus ParseAndValidateBlob(
      const Source* source,
      BlobRef blob,
      std::unique_ptr<LockedSettingsContainer>* container) const;

  // Revalidates a document, including trust configuration and signature checks.
  // Returns true if the document is still valid against current trust
  // configuration.
  bool RevalidateDocument(const Source* source,
                          const SettingsDocument* doc) const;

  // Re-validate all documents belonging to a source. Documents that fail
  // validation are removed from the SettingsMap, the SettingsDocument is
  // deleted and the corresponding blob purged from the BlobStore. The keys that
  // have changed due to the removal are added to |changed_keys| and the
  // |sources_to_revalidate| queue is updated accordingly.
  void RevalidateSourceDocuments(
      const SourceMapEntry& entry,
      std::set<Key>* changed_keys,
      std::priority_queue<std::string>* sources_to_revalidate);

  // Updates trust configuration after |changed_keys| adjust their value. This
  // re-parses all the source configurations affected by the change and
  // re-verifies the settings documents belong to these sources.
  //
  // If any documents become invalid, they'll be removed. This may cascade to
  // trigger further source changes. The process is guaranteed to terminate
  // though as a source may only update trust configuration for lower-priority
  // sources.
  //
  // The |changed_keys| set is updated to include any additional changes caused
  // by cascading removals.
  void UpdateTrustConfiguration(std::set<Key>* changed_keys);

  // Look up the source with the provided |source_id|. Returns |nullptr| if
  // there's no such source.
  const Source* FindSource(const std::string& source_id) const;

  // The parser used to decode binary settings blobs.
  const SettingsBlobParserFunction settings_blob_parser_function_;

  // The source delegate factory.
  const SourceDelegateFactoryFunction source_delegate_factory_function_;

  // The trusted document that bootstraps trust configuration.
  std::unique_ptr<const SettingsDocument> trusted_document_;

  // The BlobStore responsible for storing, loading and enumerating settings
  // blobs.
  BlobStore blob_store_;

  // A map of all sources currently present, along with their documents.
  std::map<std::string, SourceMapEntry> sources_;

  // The underlying settings map that tracks effective configuration.
  std::unique_ptr<SettingsMap> settings_map_;

  base::ObserverList<SettingsObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(SettingsDocumentManager);
};

}  // namespace settingsd

#endif  // SETTINGSD_SETTINGS_DOCUMENT_MANAGER_H_
