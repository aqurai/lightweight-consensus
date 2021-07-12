

#include "consensus-manager.h"
#include <strings.h>
#include <math.h>

ConsensusManager::ConsensusManager(int self_id, ReplicatedLog *replog) :
self_id_(self_id),
replog_(replog) {
  system_ = new SystemInterface(self_id);
}

void ConsensusManager::receiveMessage(std::string msg) {
  DataPacket pkt(msg);
  replog_->addMessage(pkt);

  // Check commit of our entry or else add it back to buffer
  if (proposed_eid > 0) {
    if ((uint8_t) self_id_ == replog_->getRidForEid(proposed_eid)
            && replog_->getCommitEid() >= proposed_eid) {
      // It has been committed.
      system_->printConsole("Our proposed entry %d has been committed", (int) proposed_eid);
      proposed_eid = 0;
    } else if ((uint8_t) self_id_ != replog_->getRidForEid(proposed_eid)
            && replog_->getCommitEid() >= proposed_eid) {
      // Our entry overridden by someone else
      system_->printConsole("Our proposed entry %d overridden. Transmit again later.", (int) proposed_eid);
      proposed_eid = 0;
    }
  }
  computeConsensusState();
}

std::string ConsensusManager::getMessageToSend(uint8_t *proposed_data, int len) {
  DataPacket pkt;
  if (proposed_eid == 0) {
    pkt = replog_->getPreparedPacket(proposed_data, len);
    if (!pkt.log_entry.empty() && pkt.log_entry[0].type == LogEntryPacket::LType::Default) {
      proposed_eid = pkt.log_entry[0].eid;
      system_->printConsole("Sending our proposal with eid %d", (int)proposed_eid);
    }
  } else {
    pkt = replog_->getPreparedPacket();
  }
  return pkt.serialize();
}

void ConsensusManager::computeConsensusState() {
  for (int i = 1; i <= replog_->getCommitEid(); i++) {
    auto data = replog_->getLogDataByEid(i);
    
    // Process data
  }
}