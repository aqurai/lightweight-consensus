# Lightweight Consensus Protocol

Lightweight consensus protocol for robots with communication constraints. Consensus is achieved by way of replicated log, inspired by Raft and a host of other consensus algorithms. However, this protocol is tuned specifically for acoustic communication, which is low-bandwidth and is broadcast (all-to-all).

Originally developed for consensus among a team of underwater robots. Communication bandwidth is extremely low, of the order of tens of bytes per second. Our goal is to use consensus to coordinate actions of the robots during multi-robot adaptive sampling in lakes.


