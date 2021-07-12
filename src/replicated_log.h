/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   replicated_log.h
 * Author: anwar
 *
 * Created on November 12, 2019, 10:14 AM
 */

#ifndef REPLICATED_LOG_H
#define REPLICATED_LOG_H

#include "sys_interface.h"
#include "data_structs.h"

#include <map>
#include <vector>

const uint8_t RETRANSMIT_REQ = 2; // Arg Midx
const uint8_t RETRANSMIT_RESP = 3; // args Data, is_committed


class ReplicatedLog {
public:
  ReplicatedLog(uint8_t id, size_t num_peers, double slot_len_sec);
  
  void addMessage(DataPacket packet);
  
  DataPacket getPreparedPacket();
  DataPacket getPreparedPacket(uint8_t *dat, int len);
  
  void updateCommit();
  void updateAbort();
  
  void printLog();
  void writeToFile(std::string file_prefix);
  
  uint32_t getCommitEid();
  uint32_t getLastEid();
  uint8_t getRidForEid(uint8_t eid);
  
  std::vector<uint8_t> getLogDataByEid(uint8_t eid);
  
//  bool isRetransmitReqPending() { return (retransmit_req_eid_queue_.size() > 0);}
//  bool isRetransmitRespPending() { return (retransmit_resp_eid_queue_.size() > 0);}
  
private:
  
  std::vector<LogEntry> log_entries_;
  std::set<uint8_t> retransmit_req_eid_queue_;
  std::set<uint8_t> retransmit_resp_eid_queue_;
  std::vector<Vote> vote_queue_;
  
  size_t num_peers_;
  double tx_slot_len_s_;
  size_t majorityPeers();
  bool isCompatibleSuccessor(const LogEntry& e);
  void handleVote(uint8_t sender_rid, uint8_t rid, uint8_t eid, Vote::VoteValue vote_value);
  
  LogEntry* getLogEntryPtr(uint8_t rid, uint8_t eid);
  LogEntry* getLogEntryPtrByEid(uint8_t eid);
  
  bool markEntryForCommit(uint8_t rid, uint8_t eid);
  void checkCandidates();
  void handleMissingCommittedEntries(uint8_t other_commit_rid, uint8_t other_commit_eid);
  void handleRetransmitResponse(LogEntry e, uint8_t their_commit_idx);

  // Manage commit indices
  uint32_t commit_idx_;
  uint64_t total_message_count_;
  uint32_t leading_commit_midx;
  bool isAllComitted();

  // Candidates
  std::vector<LogEntry> candidate_entries_;
  LogEntry* getCandidateLogEntryPtr(uint8_t rid, uint8_t eid);
  std::vector<LogEntry*> getCandidateLogEntryPtrList(uint32_t eid);
  void removeCandidateByRidEid(uint8_t rid, uint8_t eid);
  void removeCandidatesByEid(uint8_t eid);
  void handleVoteCandidate(uint8_t sender_rid, uint8_t rid, uint8_t eid, Vote::VoteValue vote_value);
  
  SystemInterface *system_;

};

#endif /* REPLICATED_LOG_H */

