
/* 
 * File:   consensus-manager.h
 * Author: anwar
 *
 * Created on October 7, 2020, 7:00 PM
 */

#ifndef CONSENSUS_MANAGER_H
#define CONSENSUS_MANAGER_H

#include "replicated_log.h"
#include "data_structs.h"

class ConsensusManager {
public:
  ConsensusManager(int self_id, ReplicatedLog *replog);

  void receiveMessage(std::string msg);

  std::string getMessageToSend(uint8_t *proposed_data, int len);

  void computeConsensusState();

private:
  uint8_t proposed_eid = 0;
  uint8_t self_id_;
  ReplicatedLog *replog_;

  SystemInterface *system_;
};

#endif /* CONSENSUS_MANAGER_H */
