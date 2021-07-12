/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   data_structs.h
 * Author: anwar
 *
 * Created on October 7, 2020, 5:11 PM
 */

#ifndef DATA_STRUCTS_H
#define DATA_STRUCTS_H

#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <set>
#include <stdint.h>

class LogEntryPacket {
public:
  uint8_t rid;
  uint8_t eid;
  uint8_t prev_rid; 
  uint8_t prev_eid;
  std::vector<uint8_t> data;
  enum class LType {Default, Revote, Retransmission};
  LType type;
};


class LogEntry {
public:
  uint8_t initiator_rid;
  uint8_t rid;
  uint8_t eid;
  uint8_t prev_rid; 
  uint8_t prev_eid;
  std::vector<uint8_t> data;
  std::set<uint8_t> voters;
  std::set<uint8_t> decliners;
  enum class CommitStatus {Uncommitted, ReadyToCommit, Committed, Aborted};
  CommitStatus commit_status = CommitStatus::Uncommitted;
  double recv_time_s;
};

class Vote {
public:
  uint8_t rid;
  uint8_t eid;
  enum class VoteValue {Decline, Accept, Committed, Abort};
  VoteValue vote_value;
};


class DataPacket {

public:
   DataPacket():
            sender_rid(0),
            commit_rid(0), 
            commit_eid(0) {
            
            }
    DataPacket(std::string serialized);
    std::string serialize();
    uint8_t sender_rid;
    uint8_t commit_rid;
    uint8_t commit_eid;
    std::vector<LogEntryPacket> log_entry;
    std::vector<Vote> votes_;
    std::vector<uint8_t> retrans_request;
};





//MessageConsensusData replogPacketToMessage(DataPacket d);
//DataPacket messageToReplogPacket(MessageConsensusData d);

#endif /* DATA_STRUCTS_H */

