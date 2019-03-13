// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "summaryadapter.h"
#include <vespa/searchcore/proton/docsummary/summarymanager.h>
#include <vespa/vespalib/objects/nbostream.h>

#include <vespa/log/log.h>
LOG_SETUP(".proton.server.summaryadapter");

using namespace document;

namespace proton {

SummaryAdapter::SummaryAdapter(const SummaryManager::SP &mgr)
    : _mgr(mgr),
      _lastSerial(_mgr->getBackingStore().lastSyncToken())
{}

SummaryAdapter::~SummaryAdapter() {}

bool SummaryAdapter::ignore(SerialNum serialNum) const
{
    assert(serialNum != 0);
    return serialNum <= _lastSerial;
}

ISummaryManager & SummaryAdapter::imgr() const { return *_mgr; }

void
SummaryAdapter::put(SerialNum serialNum, const DocumentIdT lid, const Document &doc)
{
    if ( ! ignore(serialNum) ) {
        LOG(spam, "SummaryAdapter::put(docId = '%s', lid = %u, document = '%s')",
            doc.getId().toString().c_str(), lid, doc.toString(true).c_str());
        _mgr->putDocument(serialNum, lid, doc);
        _lastSerial = serialNum;
    }
}

void
SummaryAdapter::put(SerialNum serialNum, const DocumentIdT lid, const vespalib::nbostream &os)
{
    if ( ! ignore(serialNum) ) {
        LOG(spam, "SummaryAdapter::put(serialnum = '%" PRIu64 "', lid = %u, stream size = '%zd')",
            serialNum, lid, os.size());
        _mgr->putDocument(serialNum, lid, os);
        _lastSerial = serialNum;
    }
}

void
SummaryAdapter::remove(SerialNum serialNum, const DocumentIdT lid)
{
    if ( ! ignore(serialNum + 1) ) {
        _mgr->removeDocument(serialNum, lid);
        _lastSerial = serialNum;
    }
}

void
SummaryAdapter::heartBeat(SerialNum serialNum)
{
    if (serialNum > _lastSerial) {
        remove(serialNum, 0u); // XXX: Misused lid 0
    }
}

const search::IDocumentStore &
SummaryAdapter::getDocumentStore() const {
    return imgr().getBackingStore();
}

std::unique_ptr<Document>
SummaryAdapter::get(const DocumentIdT lid, const DocumentTypeRepo &repo) {
    return imgr().getBackingStore().read(lid, repo);
}

void
SummaryAdapter::compactLidSpace(uint32_t wantedDocIdLimit) {
    _mgr->getBackingStore().compactLidSpace(wantedDocIdLimit);
}

} // namespace proton
