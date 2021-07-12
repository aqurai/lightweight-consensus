/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "replicated_log.h"
#include <assert.h>
#include <fstream>
#include <iostream>


ReplicatedLog::ReplicatedLog(uint8_t id, size_t num_peers, double slot_len_sec) {
  
  LogEntry base_entry;
  base_entry.rid = 0;
  base_entry.eid = 0;
  base_entry.commit_status = LogEntry::CommitStatus::Committed;
  base_entry.recv_time_s = system_->getTimeBootUs() / kSecondsToMicros;
  base_entry.data.push_back(0);
  
  log_entries_.push_back(base_entry);
  
  commit_idx_ = 0;
  num_peers_ = num_peers;
  tx_slot_len_s_ = slot_len_sec;
  total_message_count_ = 0;
  system_ = new SystemInterface(id);
}

void ReplicatedLog::addMessage(DataPacket packet) {
  
  // 2. Check other commit status
  if (getCommitEid() < packet.commit_eid) {
    // Find entries and mark them committed by other or committed. change commit idx.
    LogEntry *e_entry = getLogEntryPtr(packet.commit_rid, packet.commit_eid);
    if (e_entry != NULL) {
      markEntryForCommit(packet.commit_rid, packet.commit_eid);
    } else {
      system_->printConsole("\t\tMissing entry idx %d committed by others {%d,%d}. Remove following entries from log.", (int)packet.commit_eid, (int)packet.commit_rid, (int)packet.commit_eid);
      if (log_entries_.size() >= packet.commit_eid+1) {
        log_entries_.resize(packet.commit_eid);
      }
      
      //retransmit_req_eid_queue_.insert(packet.commit_eid);
    }
  }
  handleMissingCommittedEntries(packet.commit_rid, packet.commit_eid);
  
  // 3. Check votes
  if (!packet.votes_.empty()) {
    for (auto& v : packet.votes_) {
      handleVote(packet.sender_rid, v.rid, v.eid, v.vote_value);
      handleVoteCandidate(packet.sender_rid, v.rid, v.eid, v.vote_value);
    }
  }
  
  // 4. Check retransmission requests
  if (!packet.retrans_request.empty()) {
    for (uint8_t e : packet.retrans_request) {
      if (e <= getCommitEid()) {
        system_->printConsole("\t\t\t  Accepted retrans req of eid %d from robot %d. Adding to queue", (int)e, (int)packet.sender_rid);
        retransmit_resp_eid_queue_.insert(e);
      }
    }
  }
  
  // Check if something is ready to commit.
  updateCommit();
  updateAbort();
  
  
  // 1. If there are entries, add them.
  if (!packet.log_entry.empty()) {
    LogEntryPacket epkt = packet.log_entry[0];
    LogEntry logentry;
    logentry.initiator_rid = packet.sender_rid;
    logentry.rid = epkt.rid;
    logentry.eid = epkt.eid;
    logentry.prev_rid = epkt.prev_rid;
    logentry.prev_eid = epkt.prev_eid;
    logentry.data = epkt.data;
    logentry.voters.insert(epkt.rid);
    logentry.voters.insert(packet.sender_rid);
    logentry.commit_status = LogEntry::CommitStatus::Uncommitted;
    logentry.recv_time_s = system_->getTimeBootUs() / kSecondsToMicros;

    // 1.A. New log entry: If compatible, add packet to main log entry, add self to voters.
    if (epkt.type == LogEntryPacket::LType::Default &&
            isCompatibleSuccessor(logentry)) {
      //logentry.voters.insert(system_->self_id);
      log_entries_.push_back(logentry);
      system_->printConsole("\t\tAdding entry {%d,%d} sent by %d", (int)epkt.rid, (int)epkt.eid, (int)packet.sender_rid);
    } else if (epkt.type == LogEntryPacket::LType::Default &&
                !isCompatibleSuccessor(logentry)) {
      system_->printConsole("\t\tIncompatible entry {%d,%d} sent by %d. Our previous is {%d,%d}. Add to candidate list", (int)epkt.rid, (int)epkt.eid, (int)packet.sender_rid, (int)log_entries_.back().rid, (int)log_entries_.back().eid);
      candidate_entries_.push_back(logentry);
    }

    // 1.B. Re vote: If this is a recovery phase, handle 
    if (epkt.type == LogEntryPacket::LType::Revote) {
      Vote v;
      v.rid = epkt.rid;
      v.eid = epkt.eid;
      v.vote_value = Vote::VoteValue::Decline;

      LogEntry *e_entry = getLogEntryPtr(epkt.rid, epkt.eid);

      // If we have this entry already
      if (e_entry != NULL) {
        e_entry->initiator_rid = packet.sender_rid;
        e_entry->recv_time_s = system_->getTimeBootUs() / kSecondsToMicros;
        if (e_entry->commit_status == LogEntry::CommitStatus::Uncommitted) {
          v.vote_value = Vote::VoteValue::Accept;
        } else if (e_entry->commit_status == LogEntry::CommitStatus::Committed) {
          v.vote_value = Vote::VoteValue::Committed;
        }
      }

      // We dont have this entry already
      else {
        if (isCompatibleSuccessor(logentry)) {
          logentry.voters.insert(system_->self_id);
          log_entries_.push_back(logentry);
          v.vote_value = Vote::VoteValue::Accept;
          system_->printConsole("Added new entry {%d, %d} during revote request", (int)logentry.rid, (int)logentry.eid); 
        }
      }

      vote_queue_.push_back(v);
    }


    // 1.C. Retransmission response.
    if (epkt.type == LogEntryPacket::LType::Retransmission) {
      handleRetransmitResponse(logentry, packet.commit_eid);
    }
  }
  
  // Check candidates for uncommitted but compatible entries.
  checkCandidates();
  updateCommit();
  updateAbort();
  
}



void ReplicatedLog::updateCommit() {
  if (isAllComitted()) {
    return;
  }

  for (size_t i = getCommitEid() + 1; i < log_entries_.size(); ++i) {
    if (log_entries_.at(i).voters.size() >= majorityPeers() ||
            log_entries_.at(i).commit_status == LogEntry::CommitStatus::Committed) {

      log_entries_.at(i).commit_status = LogEntry::CommitStatus::Committed;
      commit_idx_ = i;
      system_->printConsole("\t\t  Committing entry %d", (int)i);
    } else {
      break;
    }
  }
}

void ReplicatedLog::updateAbort() {
  if (isAllComitted()) {
    return;
  }
  for (auto it = log_entries_.begin()+getCommitEid()+1; it != log_entries_.end(); ) {
    size_t accepts = it->voters.size();
    size_t declines = it->decliners.size();
    
    bool previously_aborted = false;
    
    if (previously_aborted && it->commit_status == LogEntry::CommitStatus::Committed) {
      system_->printConsole("FATAL: Older entry aborted but this entry {%d,%d} was aleady committed ", (int)it->rid, (int)it->eid);
    }
    
    if (previously_aborted || (declines >= majorityPeers() && 
                              it->commit_status != LogEntry::CommitStatus::Aborted &&
                              it->commit_status != LogEntry::CommitStatus::Committed)) 
    {
      
      it->commit_status = LogEntry::CommitStatus::Aborted;
      Vote v;
      v.rid = it->rid;
      v.eid = it->eid;
      v.vote_value = Vote::VoteValue::Abort;
      vote_queue_.push_back(v);
      
      if (!previously_aborted) {
        system_->printConsole("\t\t  Marking entry {%d,%d} as Aborted due to majority decline votes", (int)it->rid, (int)it->eid);
      } else {
        system_->printConsole("\t\t  Marking entry {%d,%d} as Aborted due to older entry being aborted", (int)it->rid, (int)it->eid);
      }
    }
    
    
    
    if (it->commit_status == LogEntry::CommitStatus::Aborted) {
      previously_aborted = true;
      system_->printConsole("\t\t  Erasing aborted entry {%d,%d}", (int)it->rid, (int)it->eid);
      it = log_entries_.erase(it);
    } else {
      ++it;
    }

  }
}



void ReplicatedLog::printLog() {
  system_->printConsole("Printing log ====================================");
  for (const auto& e : log_entries_) {
    std::cout << (int)e.rid << ":" << (int)e.eid << " | " << (int)e.data.front() << "\n";
  }
}

DataPacket ReplicatedLog::getPreparedPacket() {
  DataPacket pkt;
  pkt.sender_rid = system_->self_id;
  pkt.commit_eid = getCommitEid();
  LogEntry *e_committed = getLogEntryPtrByEid(getCommitEid());
  if (e_committed != NULL) {
    pkt.commit_rid = e_committed->rid;
  } else {
    system_->printConsole("FATAL: Committed index %d is Null entry", (int)getCommitEid());
  }
  
  // Fill in votes
  for (size_t i = getCommitEid() + 1; i < log_entries_.size(); ++i) {
    Vote v;
    v.rid = log_entries_.at(i).rid;
    v.eid = log_entries_.at(i).eid;
    v.vote_value = Vote::VoteValue::Accept;
    pkt.votes_.push_back(v);
  }
  pkt.votes_.insert(pkt.votes_.end(), vote_queue_.begin(), vote_queue_.end());
  vote_queue_.clear();
  
  
  // Revote request packet
  for (size_t i = getCommitEid() + 1; i < log_entries_.size(); ++i) {
    double dur_s = (system_->getTimeBootUs() / kSecondsToMicros) - log_entries_.at(i).recv_time_s;
    size_t accepts = log_entries_.at(i).voters.size();
    size_t declines = log_entries_.at(i).decliners.size();
    bool is_uncommitted = (log_entries_.at(i).commit_status == LogEntry::CommitStatus::Uncommitted);
    if (dur_s > num_peers_ * tx_slot_len_s_ && 
            is_uncommitted && 
            accepts < majorityPeers() && 
            declines < majorityPeers()) {
      
      LogEntryPacket lepkt;
      lepkt.rid = log_entries_.at(i).rid;
      lepkt.eid = log_entries_.at(i).eid;
      lepkt.prev_rid = log_entries_.at(i-1).rid;
      lepkt.prev_eid = log_entries_.at(i-1).eid;
      lepkt.data = log_entries_.at(i).data;
      lepkt.type = LogEntryPacket::LType::Revote;
      pkt.log_entry.push_back(lepkt);
      system_->printConsole(">>>> Entry {%d,%d} Pending for %f s. Has insufficient votes (%d) and Declines (%d), Sending for revote.", (int)lepkt.rid, (int)lepkt.eid, dur_s, (int)accepts, (int)declines);
      break;
    }
  }
  
  // Retransmission request / response
  if (!retransmit_req_eid_queue_.empty()) {
    for (auto e : retransmit_req_eid_queue_) {
      system_->printConsole(">>>> Request retransmission of %d.", (int)e);
      pkt.retrans_request.push_back(e);
      if (pkt.retrans_request.size() > 5) {
        break;
      }
    }
  } else if (pkt.log_entry.empty() && !retransmit_resp_eid_queue_.empty()) {
    auto e = retransmit_resp_eid_queue_.begin();
    LogEntry *e_retrans = getLogEntryPtrByEid(*e);
    LogEntry *e_prev = getLogEntryPtrByEid(*e-1);
    
    if (e_retrans == NULL) {
      system_->printConsole("FATAL RTx response for {%d, %d} - This entry is NULL. This is expected to be committed.");
    } else if (e_prev == NULL) {
      system_->printConsole("FATAL RTx response for {%d, %d} - Prev entry is NULL.");
    } else if (e_retrans->commit_status == LogEntry::CommitStatus::Committed) {
      LogEntryPacket lepkt;
      lepkt.rid = e_retrans->rid;
      lepkt.eid = e_retrans->eid;
      lepkt.prev_rid = e_prev->rid;
      lepkt.prev_eid = e_prev->eid;
      lepkt.data = e_retrans->data;
      lepkt.type = LogEntryPacket::LType::Retransmission;
      pkt.log_entry.push_back(lepkt);
      retransmit_resp_eid_queue_.erase(e);
      system_->printConsole(">>>> Adding Retx response of {%d,%d} to transmission", (int)lepkt.rid, (int)lepkt.eid);
    }
  }
  
  return pkt;
  
}



DataPacket ReplicatedLog::getPreparedPacket(uint8_t *dat, int len) {
  DataPacket pkt = getPreparedPacket();
  
  if (!retransmit_req_eid_queue_.empty()) {
    system_->printConsole(">>>> Ignoring add data packet req since retx request is pending. We are lagging.");
  } else if (!pkt.log_entry.empty()) {
    system_->printConsole(">>>> Ignoring add data packet req since already added log entry for revote/retransmission resp..");
  } else {
  
    LogEntryPacket epkt;
    epkt.rid = system_->self_id;
    epkt.eid = log_entries_.back().eid+1;
    epkt.prev_rid = log_entries_.back().rid;
    epkt.prev_eid = log_entries_.back().eid;
    for (int i=0; i<len; ++i) {
      epkt.data.push_back(dat[i]);
    }
    epkt.type = LogEntryPacket::LType::Default;

    pkt.log_entry.push_back(epkt);

    LogEntry logentry;
    logentry.initiator_rid = system_->self_id;
    logentry.eid = epkt.eid;
    logentry.rid = epkt.rid;
    logentry.prev_eid = epkt.prev_eid;
    logentry.prev_rid = epkt.prev_rid;
    logentry.data = epkt.data;
    logentry.voters.insert(system_->self_id);
    logentry.commit_status = LogEntry::CommitStatus::Uncommitted;
    logentry.recv_time_s = system_->getTimeBootUs() / kSecondsToMicros;
    log_entries_.push_back(logentry);
    
    system_->printConsole(">>>> Transmitting new log entry {%d,%d}. Previous is {%d,%d}", (int)epkt.rid, (int)epkt.eid, (int)epkt.prev_rid, (int)epkt.prev_eid);
  }
  
  return pkt;
}




size_t ReplicatedLog::majorityPeers() {
  if (num_peers_ %2 == 0) {
    return 1 + num_peers_/2;
  } else {
    return 1 + size_t(num_peers_/2);
  } 
}

bool ReplicatedLog::isCompatibleSuccessor(const LogEntry& e) {
  LogEntry& old_e = log_entries_.back();
  return (old_e.eid == e.prev_eid && 
          old_e.rid == e.prev_rid);
}

void ReplicatedLog::handleVote(uint8_t sender_rid, uint8_t rid, uint8_t eid, Vote::VoteValue vote_value) {
  
  // Handle explicit decline
  if (vote_value == Vote::VoteValue::Decline) {
    LogEntry *e = getLogEntryPtr(rid, eid);
    if (e != NULL) {
      e->decliners.insert(sender_rid);
      e->voters.erase(sender_rid);
      system_->printConsole("\t\t\t\t\tReceived Vote %d -> DECLINE -> {%d,%d}. Accepts, declines = %d, %d", (int)sender_rid, (int)rid, (int)eid, e->voters.size(), e->decliners.size());
    } else {
      //system_->printConsole("\t\t\t\t\t\tReceived decline Vote for an absent entry {%d,%d} from  %d", (int)rid, (int)eid, (int)sender_rid);
    }
  }
  
  // Accept for a competing message is a decline. Otherwise accept.
  if (vote_value == Vote::VoteValue::Accept) {
    LogEntry *e = getLogEntryPtrByEid(eid);
    if (e != NULL) {
      if (e->rid == rid) {
        e->voters.insert(sender_rid);
        e->decliners.erase(sender_rid);
        system_->printConsole("\t\t\t\t\tReceived Vote %d -> ACCEPT -> {%d,%d}. Accepts, declines = %d, %d", (int)sender_rid, (int)rid, (int)eid, e->voters.size(), e->decliners.size());
      } else {
        // This was a vote for a competing entry, i.e against this entry
        e->decliners.insert(sender_rid);
        e->voters.erase(sender_rid);
        system_->printConsole("\t\t\t\t\tReceived Vote %d -> DECLINE -> {%d,%d}. Accepts, declines = %d, %d. (as vote for competing entry {%d,%d})", (int)sender_rid, (int)e->rid, (int)e->eid, e->voters.size(), e->decliners.size(), (int)rid, (int)eid);
      }
    } else {
      //system_->printConsole("Received accept Vote for an absent entry {%d,%d} from  %d", (int)rid, (int)eid, (int)sender_rid);
    }
  }
  
  // Ready to commit
  if (vote_value == Vote::VoteValue::Committed) {
    bool res = markEntryForCommit(rid, eid);
    if (!res) {
      system_->printConsole("\t\t\t\t\tReceived Vote %d -> COMMIT -> {%d,%d}", (int)sender_rid, (int)rid, (int)eid);
    } else {
      //system_->printConsole("\t\t\t\t\t\tReceived commit for an entry {%d,%d} that we dont have from %d", (int)rid, (int)eid, (int)sender_rid);
    }
  }
  
  // Abort
  if (vote_value == Vote::VoteValue::Abort) {
    LogEntry *e = getLogEntryPtr(rid, eid);
    if (e != NULL) {
      if (e->commit_status == LogEntry::CommitStatus::Committed) {
        system_->printConsole("FATAL: Entry {%d,%d} was already RTC/Committed but received vote for abort from %d.", (int)rid, (int)eid, (int)sender_rid);
      }
      e->commit_status = LogEntry::CommitStatus::Aborted;
      system_->printConsole("\t\t\t\t\tReceived Vote %d -> ABORT -> {%d,%d}", (int)sender_rid, (int)rid, (int)eid);
    } else {
      system_->printConsole("\t\t\t\t\t\tReceived Abort for an entry {%d,%d} that we dont have from %d", (int)rid, (int)eid, (int)sender_rid);
    }
    updateAbort();
  }
}


LogEntry* ReplicatedLog::getLogEntryPtr(uint8_t rid, uint8_t eid) {
    LogEntry* e_ptr = NULL;
    for (auto it = log_entries_.begin(); it != log_entries_.end(); ++it) {
      if (it->eid == eid && it->rid == rid) {
          e_ptr = &(*it);
      }
    }
    return e_ptr;
}

LogEntry* ReplicatedLog::getLogEntryPtrByEid(uint8_t eid) {
  LogEntry* e_ptr = NULL;
  for (auto it = log_entries_.begin(); it != log_entries_.end(); ++it) {
    if (it->eid == eid) {
        e_ptr = &(*it);
    }
  }
  return e_ptr;
}

bool ReplicatedLog::markEntryForCommit(uint8_t rid, uint8_t eid) {
  LogEntry *e_this = getLogEntryPtr(rid, eid);
  if (e_this != NULL) {
    e_this->commit_status = LogEntry::CommitStatus::Committed;
    system_->printConsole("\t\t  Marking {%d, %d} for commit", (int)e_this->rid, (int)e_this->eid);
    updateCommit();
  }
  return (e_this != NULL);
}

void ReplicatedLog::checkCandidates() {
  uint8_t max_cand_eid = 0;
  for (auto c : candidate_entries_) {
    if (c.eid > max_cand_eid) {
      max_cand_eid = c.eid;
    }
  }
  
  for (uint8_t eid = log_entries_.back().eid+1; eid<=max_cand_eid; eid++) {
    if (system_->self_id == 1) system_->printConsole("looking for eid %d in candidates", eid);
    std::vector<LogEntry*> c_ptrs = getCandidateLogEntryPtrList(eid);
    LogEntry *potential_c = NULL;
    for (auto c_ptr : c_ptrs) {
      if (system_->self_id == 1) system_->printConsole("Candidate {%d,%d}, previous {%d,%d}", (int)c_ptr->rid, (int)c_ptr->eid, (int)c_ptr->prev_rid, (int)c_ptr->prev_eid);
      if (isCompatibleSuccessor(*c_ptr) && (potential_c == NULL || c_ptr->voters.size() > potential_c->voters.size() ) ) {
        potential_c = c_ptr;
      }
    }
    if (potential_c != NULL) {
      log_entries_.push_back(*potential_c);
      retransmit_req_eid_queue_.erase(potential_c->eid);
      system_->printConsole("\t\tAdding uncommitted entry {%d,%d} from candidates. ", (int)potential_c->rid, (int)potential_c->eid);
      removeCandidateByRidEid(potential_c->rid, potential_c->eid);
    } else {
      break;
    }
  }
}


void ReplicatedLog::handleMissingCommittedEntries(uint8_t other_commit_rid, uint8_t other_commit_eid) {
  // If the latest committed entry is present in candidates, add to log and commit.
  if (log_entries_.back().eid == other_commit_eid-1) {
    LogEntry *e = getCandidateLogEntryPtr(other_commit_rid, other_commit_eid);
    if (e!=NULL && isCompatibleSuccessor(*e)) {
      e->commit_status = LogEntry::CommitStatus::Committed;
      log_entries_.push_back(*e);
      system_->printConsole("\t\tFound missing entry {%d,%d} in candidates", (int)e->rid, (int)e->eid);
      removeCandidatesByEid(e->eid);
      retransmit_req_eid_queue_.erase(e->eid);
      return;
    }
  }
  
  
  for (uint8_t eid = log_entries_.back().eid+1; eid <= other_commit_eid; eid++ ) {
    std::vector<LogEntry*> ce_ptrs = getCandidateLogEntryPtrList(eid);
    bool found = false;
    for (auto e : ce_ptrs) {
      if (e->voters.size() >= majorityPeers() || e->commit_status == LogEntry::CommitStatus::Committed) {
        if (isCompatibleSuccessor(*e)) {
          log_entries_.push_back(*e);
          retransmit_req_eid_queue_.erase(eid);
          found = true;
          system_->printConsole("\t\tFound missing entry {%d,%d} in candidates", (int)e->rid, (int)e->eid);
        }
      }
    }
    if (found) {
      removeCandidatesByEid(eid);
      
    } else {
      system_->printConsole("\t\t  Retx req: Adding missing entry id %d to request queue", eid);
      retransmit_req_eid_queue_.insert(eid);
    }
  }
}

void ReplicatedLog::handleRetransmitResponse(LogEntry e, uint8_t their_commit_idx) {
  // If it was in our response queue, remove it.
  retransmit_resp_eid_queue_.erase(e.eid);
  
  // Did I request this?
  bool it_was_me = false;
  if (retransmit_req_eid_queue_.find(e.eid) != retransmit_req_eid_queue_.end()) {
    it_was_me = true;
    
  }
  
  if (it_was_me) {
    system_->printConsole("  ### Received retransmission of entry {%d,%d} from: %d", (int)e.rid, (int)e.eid, (int)e.initiator_rid);
    if (their_commit_idx >= e.eid) {
        e.commit_status = LogEntry::CommitStatus::Committed;
        bool appended = false;
        LogEntry *e_existing = getLogEntryPtrByEid(e.eid);
        if (e_existing!=NULL) {
          system_->printConsole("            RTx resp: Accepted AND REPLACING an existing entry {%d,%d} with retransmitted entry {%d,%d}.", (int)e_existing->rid, (int)e_existing->eid, (int)e.rid, (int)e.eid);
          *e_existing = e;
          appended = true;
          system_->printConsole("            RTx resp: Deleting entries following {%d,%d} ", (int)e_existing->rid, (int)e_existing->eid);
          log_entries_.resize(e_existing->eid + 1);
          
        } else if (isCompatibleSuccessor(e)) {
          log_entries_.push_back(e);
          appended = true;
        }
        if (appended) {
          retransmit_req_eid_queue_.erase(e.eid);
          system_->printConsole("            RTx resp: Accepted retransmission of message %d ", (int)e.eid);
        } else {
          system_->printConsole("            RTx resp: Rcvd out of order retransmission. Not handled. eid %d", (int)e.eid);
        }
      } else {
        system_->printConsole("            RTx resp: Rcvd entry that is not committed. eid: %d", (int)e.eid);
      }
  }
}

uint32_t ReplicatedLog::getCommitEid() {
  return commit_idx_;
}

uint32_t ReplicatedLog::getLastEid() {
  return log_entries_.back().eid;
}

uint8_t ReplicatedLog::getRidForEid(uint8_t eid) {
  LogEntry *e_ptr = getLogEntryPtrByEid(eid);
  if (e_ptr == NULL) {
    system_->printConsole("Querying for eid that is null %d", (int) eid);
    return 254;
  }
  return e_ptr->rid;
}

std::vector<uint8_t> ReplicatedLog::getLogDataByEid(uint8_t eid) {
  LogEntry *e_ptr = getLogEntryPtrByEid(eid);
  return e_ptr->data;
}

bool ReplicatedLog::isAllComitted() {
  return (getCommitEid() + 1 >= log_entries_.size());
}

LogEntry* ReplicatedLog::getCandidateLogEntryPtr(uint8_t rid, uint8_t eid) {
    LogEntry* e_ptr = NULL;
    for (auto it = candidate_entries_.begin(); it != candidate_entries_.end(); ++it) {
      if (it->eid == eid && it->rid == rid) {
          e_ptr = &(*it);
      }
    }
    return e_ptr;
}

std::vector<LogEntry*> ReplicatedLog::getCandidateLogEntryPtrList(uint32_t eid) {
  std::vector<LogEntry*> ptrlist;
  for (auto it = candidate_entries_.begin(); it != candidate_entries_.end(); ++it) {
    if (it->eid == eid) {
      ptrlist.push_back(&(*it));
    }
  }
  return ptrlist;
}

void ReplicatedLog::removeCandidateByRidEid(uint8_t rid, uint8_t eid) {
  for (auto it = candidate_entries_.begin(); it != candidate_entries_.end();) {
    if (it->rid == rid && it->eid == eid) {
      it = candidate_entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void ReplicatedLog::removeCandidatesByEid(uint8_t eid) {
  for (auto it = candidate_entries_.begin(); it != candidate_entries_.end();) {
    if (it->eid == eid) {
      it = candidate_entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void ReplicatedLog::handleVoteCandidate(uint8_t sender_rid, uint8_t rid, uint8_t eid, Vote::VoteValue vote_value) {
  LogEntry *e = getCandidateLogEntryPtr(rid, eid);
  if (e != NULL) {
    if (vote_value == Vote::VoteValue::Accept) {
      e->voters.insert(sender_rid);
      e->decliners.erase(sender_rid);
    } else if (vote_value == Vote::VoteValue::Decline) {
      e->voters.erase(sender_rid);
      e->decliners.insert(sender_rid);
    } else if (vote_value == Vote::VoteValue::Committed) {
      e->commit_status = LogEntry::CommitStatus::Committed;
      system_->printConsole("Candidate Entry Ready to commit {%d,%d}", (int)rid, (int)eid);
    } else if (vote_value == Vote::VoteValue::Abort) {
      removeCandidateByRidEid(rid, eid);
    }
    
    if (e->decliners.size() >= majorityPeers()) {
      removeCandidateByRidEid(rid, eid);
    }
  }
  
  std::vector<LogEntry*> e_list = getCandidateLogEntryPtrList(eid);
  for (auto e_ptr : e_list) {
    if (e_ptr->rid != rid) {
      e_ptr->voters.erase(sender_rid);
      e_ptr->decliners.insert(sender_rid);
    }
  }
  
  for (auto it = candidate_entries_.begin(); it != candidate_entries_.end();) {
    if (it->decliners.size() >= majorityPeers()) {
      it = candidate_entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void ReplicatedLog::writeToFile(std::string file_prefix) {
  /*std::string filename = file_prefix + "_A" + std::to_string(system_->self_id) + ".txt";
  std::ofstream filestr;
  filestr.open(filename);
  for (const auto& e : log_entries_) {
    uint32_t retrans_req_sent = 0;
    uint32_t retrans_resp_sent = 0;
    
    if (ldat.pkt_num_retrans_request_sent.count(e.eid) > 0 ) {
      retrans_req_sent = ldat.pkt_num_retrans_request_sent[e.eid];
    }
    if (ldat.pkt_num_retrans_response_sent.count(e.eid) > 0 ) {
      retrans_resp_sent = ldat.pkt_num_retrans_response_sent[e.eid];
    }
    
    filestr << (int)e.rid << ", " << (int)e.eid << ", " << ldat.committed_packet_recv_time_s[e.eid] << ", " << ldat.committed_packet_commit_time_s[e.eid] << ", " << retrans_req_sent << ", " << retrans_resp_sent << "\n";
  }
  */
}


