#include "b4mesh-raft.h"

NS_LOG_COMPONENT_DEFINE("B4MeshRaft");
NS_OBJECT_ENSURE_REGISTERED(B4MeshRaft);

TypeId B4MeshRaft::GetTypeId(){
  static TypeId tid = TypeId("B4MeshRaft")
    .SetParent<Application>()
    .SetGroupName("Application")
    .AddConstructor<B4MeshRaft>()
    ;
  return tid;
}

B4MeshRaft::B4MeshRaft(){
  // Initialize variables
  current_state = FOLLOWER;
  current_term = 0;
  voted_for = -1;
//  current_leader = -1;
  commit_index = -1;
  last_applied = -1;
  gathered_votes = 0;
  running = false;
  majority = 0;

  // Modifiable parameters
  heartbeat_freq = 1;     // Given in seconds
  election_timeout_parameters = make_pair(4000, 6000);
  election_timeout_event = EventId();

  // Initialize trace variables
  start_election = -1;
  end_election = -1;
  received_bytes = make_pair(-1.0, -1);
  sent_bytes = make_pair(-1.0, -1);
  received_messages = make_pair(-1.0, -1);
  sent_messages = make_pair(-1.0, -1);
}

B4MeshRaft::~B4MeshRaft(){
}

void B4MeshRaft::SetUp(Ptr<Node> node, vector<Ipv4Address> peers, int hbeat, int eTimeout){
    this->peers = peers;
    this->node = node;
    this->heartbeat_freq = hbeat/1000.;
    this->election_timeout_parameters = make_pair((float)eTimeout, (float)eTimeout*1.50);

    // Pedro:
    SetCurrentLeader(-1);

  // Open the receiving socket
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  recv_sock = Socket::CreateSocket(node, tid);
  InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(),
      2000);
  recv_sock->Bind(local);
  recv_sock->SetRecvCallback (MakeCallback (&B4MeshRaft::ReceivePacket, this));

  next_indexes = vector<int>(peers.size(), 0);
  match_indexes = vector<int>(peers.size(), -1);
  commit_indexes = vector<int>(peers.size(), -1);

  // Building initial current group
  for (uint32_t i=0; i<ns3::NodeList::GetNNodes(); ++i)
    current_group.push_back(make_pair(i, GetIpAddress(i)));
}

void B4MeshRaft::ReceivePacket(Ptr<Socket> socket){
  if (!running) return;
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom(from))){
    if (!running) return;
    if (InetSocketAddress::IsMatchingType(from)){
      InetSocketAddress iaddr = InetSocketAddress::ConvertFrom(from);
      Ipv4Address ip = iaddr.GetIpv4();

      debug_suffix.str("");
      debug_suffix << "Received a new packet of " << packet->GetSize() <<
        "B from Node " << GetNodeId(ip);
      debug(debug_suffix.str());

      received_bytes = make_pair(Simulator::Now().GetSeconds(),
          packet->GetSize());
      received_messages = make_pair(Simulator::Now().GetSeconds(), 1);
      // b4m_trances.cc
      traces->ReceivedMessages(received_messages);
      traces->ReceivedBytes(received_bytes);

      string parsedPacket;
      char* packetInfo = new char[packet->GetSize()];
      packet->CopyData(reinterpret_cast<uint8_t*>(packetInfo),
          packet->GetSize());
      string parsed_packet = string(packetInfo, packet->GetSize());
      delete[] packetInfo;
      ApplicationPacket p(parsed_packet);


      if (p.GetService() == ApplicationPacket::RAFT){
        int message_type = ExtractMessageType(p.GetPayload());

        if (message_type == REQ_VOTE){
          debug_suffix.str("");
          debug_suffix << "Received a request vote " << packet->GetSize()
              << "B from Node " << GetNodeId(ip);
          debug(debug_suffix.str());

          if (!IsInCurrentGroup(ip)) {
            traces->DroppedMessages(make_pair(Simulator::Now().GetSeconds(), 1));
            debug("Message dropped because not from the same group");
            return; // Ignore all messages from nodes not in current_group
          }

          // Pedro:
          // TRACE: time   event  msg_type   node  sender  msg_size
          cout << Simulator::Now().GetSeconds() << " RECV " << "REQ_VOTE " << node->GetId() <<  " " << GetNodeId(ip) << " " << packet->GetSize() << " " << endl;


          request_vote_hdr_t req_vote =
            *((request_vote_hdr_t*)p.GetPayload().data());
          auto ret = ProcessRequestVote(req_vote);

          ApplicationPacket ret_pkt(ApplicationPacket::RAFT,
              string((char*)&ret, sizeof(ret)));

          SendPacket(ret_pkt, ip, false, "ACK_VOTE");
        }
        else if (message_type == ACK_VOTE){
          debug_suffix << "Received a request vote response from Node " <<
            GetNodeId(ip);
          debug(debug_suffix.str());

          if (!IsInCurrentGroup(ip)) {
            traces->DroppedMessages(make_pair(Simulator::Now().GetSeconds(), 1));
            debug("Message dropped because not from the same group");
            return; // Ignore all messages from nodes not in current_group
          }

          // Pedro:
          // TRACE: time   event  msg_type   node  sender  msg_size
          cout << Simulator::Now().GetSeconds() << " RECV " << "ACK_VOTE " << node->GetId() <<  " " << GetNodeId(ip) << " " << packet->GetSize() << " " << endl;


          request_vote_ack_t ack_vote =
            *((request_vote_ack_t*)p.GetPayload().data());

          ProcessVoteResponse(ack_vote);
        }
        else if (message_type == APPEND_ENTRY){
          debug_suffix << "Received an append" " entries message of " <<
            packet->GetSize() << "B from Node " << GetNodeId(ip);
          debug(debug_suffix.str());

          if (!IsInCurrentGroup(ip)) {
            traces->DroppedMessages(make_pair(Simulator::Now().GetSeconds(), 1));
            debug("Message dropped because not from the same group");
            return; // Ignore all messages from nodes not in current_group
          }

          // Pedro:
          // TRACE: time   event  msg_type   node  sender  msg_size
          cout << Simulator::Now().GetSeconds() << " RECV " << "APPEND_ENTRY " << node->GetId() <<  " " << GetNodeId(ip) << " " << packet->GetSize() << " " << endl;


          auto ret = ProcessAppendEntries(p.GetPayload());
          ApplicationPacket ret_pkt(ApplicationPacket::RAFT,
              string((char*)&ret, sizeof(ret)));

          SendPacket(ret_pkt, ip, false, "ACK_ENTRY");
        }
        else if (message_type == ACK_ENTRY){
          debug_suffix << "Received an ack entries message of " <<
            packet->GetSize() << "B from Node " << GetNodeId(ip);
          debug(debug_suffix.str());

          if (!IsInCurrentGroup(ip)) {
            traces->DroppedMessages(make_pair(Simulator::Now().GetSeconds(), 1));
            debug("Message dropped because not from the same group");
            return; // Ignore all messages from nodes not in current_group
          }

          // Pedro:
          // TRACE: time   event  msg_type   node  sender  msg_size
          cout << Simulator::Now().GetSeconds() << " RECV " << "ACK_ENTRY " << node->GetId() <<  " " << GetNodeId(ip) << " " << packet->GetSize() << " " << endl;

          append_entries_ack_t entries_ack =
            *((append_entries_ack_t*)p.GetPayload().data());
          ProcessEntriesAck(entries_ack);
        }
        else if (message_type == APPEND_ENTRY_CONF){
          debug_suffix << "Received a configuration append entries message " <<
            " of " << packet->GetSize() << "B from Node " << GetNodeId(ip);
          debug(debug_suffix.str());

          if (!IsInCurrentGroup(ip)) {
            traces->DroppedMessages(make_pair(Simulator::Now().GetSeconds(), 1));
            debug("Message dropped because not from the same group");
            return; // Ignore all messages from nodes not in current_group
          }

          // Pedro:
          // TRACE: time   event  msg_type   node  sender  msg_size
          cout << Simulator::Now().GetSeconds() << " RECV " << "APPEND_ENTRY_CONF " << node->GetId() <<  " " << GetNodeId(ip) << " " << packet->GetSize() << " " << endl;


          auto ret = ProcessAppendEntries(p.GetPayload());
          ApplicationPacket ret_pkt(ApplicationPacket::RAFT,
              string((char*)&ret, sizeof(ret)));
          SendPacket(ret_pkt, ip, false, "ACK_ENTRY");
        }
      }
    }
  }
}

void B4MeshRaft::SendPacket(ApplicationPacket& packet, Ipv4Address ip, bool
    scheduled, string msg_type){
  if (!running) return;

  if (!scheduled){
    float desync = (rand() % 50) / 1000.0;
//    float desync = 20 * GetNodeId(ip) / 1000.0;
    Simulator::ScheduleWithContext(node->GetId(), Seconds(desync),
        &B4MeshRaft::SendPacket, this, packet, ip, true, msg_type);
    return;
  }
  // Create the packet to send
  Ptr<Packet> pkt = Create<Packet>((const uint8_t*)(packet.Serialize().data()), packet.GetSize());

  // Open the sending socket
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  Ptr<Socket> source = Socket::CreateSocket (node, tid);
  InetSocketAddress remote = InetSocketAddress (ip, 2000);
  source->Connect(remote);

  // Pedro:

  // TRACE: time   event  msg_type   node  sender  msg_size
  cout << Simulator::Now().GetSeconds() << " SEND " << msg_type << " " << node->GetId() <<  " " << GetNodeId(ip) << " " << packet.GetSize() << " " << endl;


  int res = source->Send(pkt);

  sent_bytes = make_pair(Simulator::Now().GetSeconds(), pkt->GetSize());
  sent_messages = make_pair(Simulator::Now().GetSeconds(), 1);
  source->Close();

  if (res > 0){
//    debug_suffix << " Send a packet of size " << pkt->GetSize() << " to " << ip;
//    debug(debug_suffix.str());
    traces->SentMessages(sent_messages);
    traces->SentBytes(sent_bytes);
  }
  else{
    debug_suffix << "Failed to send a packet of size " << pkt->GetSize() << " to " << ip;
    debug(debug_suffix.str());
  }
}

Ipv4Address B4MeshRaft::GetIpAddress(){
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  return ipv4->GetAddress(1, 0).GetLocal();
}

Ipv4Address B4MeshRaft::GetIpAddress(int i){
  Ptr<Ipv4> ipv4 = ns3::NodeList::GetNode(i)->GetObject<Ipv4>();
  return ipv4->GetAddress(1, 0).GetLocal();
}

void B4MeshRaft::StartApplication(){
  debug_suffix << "Start B4MeshRaft on node : " << GetNode()->GetId();
  debug(debug_suffix.str());
  running = true;
  ResetElectionTimeout();
}

void B4MeshRaft::StopApplication(){
  debug_suffix << "Stop B4MeshRaft on node : " << GetNode()->GetId();
  debug(debug_suffix.str());
  debug_suffix << "Current term :" << current_term;
  debug(debug_suffix.str());
  debug_suffix << "Current last_applied :" << last_applied;
  debug(debug_suffix.str());
  debug_suffix << "Current commit_index :" << commit_index;
  debug(debug_suffix.str());
  debug_suffix << "Log size :" << log.size();
  debug(debug_suffix.str());
  debug_suffix << "Log data :[";
  for (auto l : log){
    if (groups.count(l.second) > 0)
      debug_suffix << l.second << ", ";
    else
      debug_suffix << dump(l.second.data(), 10) << ", ";
  }
  debug_suffix << "]";
  debug(debug_suffix.str());
  running = false;
}

vector<int> B4MeshRaft::GetNextIndexes(){
  return next_indexes;
}

vector<int> B4MeshRaft::GetMatchIndexes(){
  return match_indexes;
}

int B4MeshRaft::GetCommitIndex(){
  return commit_index;
}

int B4MeshRaft::GetLastApplied(){
  return last_applied;
}

vector<pair<int,string>>& B4MeshRaft::GetLog(){
  return log;
}

int B4MeshRaft::GetCurrentLeader(){
  return current_leader;
}

void B4MeshRaft::SetCurrentLeader(int ld){

  float timestamp = Simulator::Now().GetSeconds();
  if (timestamp == 0)
    timestamp = 5;

  if (ld == -1) {
    if (current_leader != -1) {
      noLeaderBegin = timestamp;
    }
  } else {
    if (current_leader == -1) {
        float noLeaderTime = timestamp - noLeaderBegin;
        // Pedro: Trace
        cout << timestamp << " NO_LEADER " << node->GetId() << " " << noLeaderTime << endl;

    }

  }
  current_leader = ld;

}


int B4MeshRaft::GetVotedFor(){
  return voted_for;
}

int B4MeshRaft::GetCurrentTerm(){
  return current_term;
}

int B4MeshRaft::GetCurrentState(){
  return current_state;
}

void B4MeshRaft::ResetElectionTimeout(float factor){
  if (!running) return;
  if (election_timeout_event != EventId())
    Simulator::Cancel(election_timeout_event);
  float delay = uniform_rand(election_timeout_parameters.first,
      election_timeout_parameters.second) / 1000.;

  delay = delay * factor;
  election_timeout_event = Simulator::Schedule(Seconds(delay), &B4MeshRaft::TriggerElectionTimeout, this);
}

void B4MeshRaft::TriggerElectionTimeout(){
  if (!running) return;
  debug_suffix << "Election timeout : Node " << node->GetId() << " will start a new election";
  debug(debug_suffix.str());
  SetCurrentLeader(-1);    // Added by David

  start_election = Simulator::Now().GetSeconds();
  traces->StartElection(start_election, node->GetId());
  ResetElectionTimeout();

  if (current_state == CANDIDATE) // Avoid yoyo effects between term and log advancement
    current_term--;

  // Avoid vote locking for a candidate that timeout.
  if (voted_for != -1){
    gathered_votes = 0;
    voted_for = -1;
    current_state = FOLLOWER;
    return;
  }

  // Apply first pending group if it is a split
  if (pending_groups.size() > 0){
    if (DetectGroupChange(pending_groups[0].second) == SPLIT)
      ApplyGroup(pending_groups[0].first);
  }

  // Initiate election process
  current_term++;
  current_state = CANDIDATE;
  voted_for = node->GetId();
  gathered_votes = 1;

  // Create request Vote packet
  request_vote_hdr_t req_vote;
  req_vote.msg_type = REQ_VOTE;
  req_vote.term = current_term;
  req_vote.candidate_id = node->GetId();
  req_vote.last_log_index = LastLogIndex(); // Replaced by last commit

  if (commit_index >= 0)
    req_vote.last_log_term = log.back().first;
  else
    req_vote.last_log_term = -1;

  ApplicationPacket pkt(ApplicationPacket::RAFT,
      string((char*)&req_vote, sizeof(req_vote)));

  for (auto& n : current_group){
    Ipv4Address ip = n.second;
    if(GetIpAddress() != ip){
      SendPacket(pkt, ip, false, "REQ_VOTE");
      debug_suffix << "Send a vote requests to node " << n.first;
      debug(debug_suffix.str());
    }
  }

  if (current_group.size() == 1){
      debug("Becomes leader for being alone in its group");

      current_state = LEADER;
      SetCurrentLeader(node->GetId());       // Added by David
      end_election = Simulator::Now().GetSeconds();
      traces->EndElection(end_election, node->GetId());
      traces->ResetStartElection();
      next_indexes = vector<int>(peers.size(), commit_index+1);
      match_indexes = vector<int>(peers.size(), 0);
      commit_indexes = vector<int>(peers.size(), -1);
      SendHeartbeats();
    //  GenerateBlock();      // commented by Pedro
  }
}

void B4MeshRaft::BroadcastPacket(ApplicationPacket& packet){

  for (auto& n : current_group){
    Ipv4Address ip = n.second;
    if(GetIpAddress() != ip)
      SendPacket(packet, ip, false);
  }
}

int B4MeshRaft::ExtractMessageType(const string& msg_payload){
  int ret = *((int*)msg_payload.data());
  return ret;
}

B4MeshRaft::request_vote_ack_t B4MeshRaft::ProcessRequestVote(request_vote_hdr_t msg){
  ResetElectionTimeout();
  request_vote_ack_t ret;
  ret.msg_type = ACK_VOTE;
  ret.term = current_term;
  ret.commit_index = commit_index;
  ret.vote_granted = 0;

  // David: Should it be less or equal to current_term
  // What happen if msg.term == current_term ?
  if (msg.term < current_term) {
    debug_suffix << "Candidate term is less advanced";
    debug(debug_suffix.str());
    return ret;
  }

  if (msg.term > current_term){
    debug_suffix <<  "Candidate term is higher";
    debug(debug_suffix.str());
    current_term = msg.term;
    current_state = FOLLOWER;
    voted_for = -1; // New term so we reset the vote;
  }

  ret.term = current_term;

  if (msg.last_log_index < LastLogIndex()){
    // Candidate log less advanced
    debug_suffix << "Candidate log less advanced";
    debug(debug_suffix.str());
    ret.vote_granted = 0;
    return ret;
  }

  if ((voted_for == -1 || voted_for == msg.candidate_id)){
    debug("Give vote");
    ret.vote_granted = 1;
    voted_for = msg.candidate_id;
  }
  else{
    debug("Refuse vote");
    ret.vote_granted = 0;
  }

  return ret;
}

int B4MeshRaft::GetLastLogTerm(){
  if (LastLogIndex() >= 0)
    return log.back().first;
  else
    return -1;
}

void B4MeshRaft::ProcessVoteResponse(request_vote_ack_t ack_vote){
  if (!running) return;
  ResetElectionTimeout();
  if (current_state == FOLLOWER) return;


  if (ack_vote.term > current_term || ack_vote.commit_index > commit_index){
    // Update current_term
    current_term = ack_vote.term;
    current_state = FOLLOWER;
  }
/* // David: No need of this IF.
  if (ack_vote.commit_index > commit_index){
    // Update current_term
    current_term = ack_vote.term;
    current_state = FOLLOWER;
  }
  */

  if (current_state == FOLLOWER){
    debug("Destituted from candidate or leader");
    RemoveUncommittedEntries();
    return;
  }

  if (current_state == LEADER)
    return;

// If node is current_state == CANDIDATE:
  if (ack_vote.vote_granted)
    gathered_votes++;
  debug_suffix << "Gathered " << gathered_votes << " votes";
  debug(debug_suffix.str());

  if ((uint32_t) gathered_votes > current_group.size() / 2){
    debug_suffix << "Become leader from a majority of " << gathered_votes << " votes";
    debug(debug_suffix.str());
    current_state = LEADER;
    SetCurrentLeader(node->GetId());
    end_election = Simulator::Now().GetSeconds();
    traces->EndElection(end_election, node->GetId());
    next_indexes = vector<int>(peers.size(), commit_index+1);
    match_indexes = vector<int>(peers.size(), 0);
    commit_indexes = vector<int>(peers.size(), 0);
    SendHeartbeats();
    GenerateBlock();
  }
}

void B4MeshRaft::SendHeartbeats(){
  if (!running) return;
  if (current_state == LEADER){
    append_entries_hdr_t entries;
    entries.msg_type = APPEND_ENTRY;
    entries.term = current_term;
    entries.leader_id = node->GetId();
    entries.prev_log_index = LastLogIndex();
    entries.prev_log_term = GetLogTerm(LastLogIndex());
    entries.leader_commit = commit_index;
    entries.entry_term = -1;

    // Pedro:
    string msg_type;

    // Apply first pending group if it is a split
    if (pending_groups.size() > 0){
      if (DetectGroupChange(pending_groups[0].second) == SPLIT)
        ApplyGroup(pending_groups[0].first);
    }
    // If all entries are committed and there is pending groups, add the first
    // one in the log
    if (LastLogIndex() == commit_index){
      if (pending_groups.size() > 0){
        if (LastLogIndex() == -1 || pending_groups[0].first != log[LastLogIndex()].second){
          debug_suffix << "Add new group (" << pending_groups[0].first << " in log";
          debug(debug_suffix.str());
          log.push_back(make_pair(current_term, pending_groups[0].first));
          match_indexes[node->GetId()] = LastLogIndex();
          next_indexes[node->GetId()] = LastLogIndex()+1;
        }
      }
    }
    // If the node is alone in the group, the entries are committed
    // immediately
    if (current_group.size() == 1)
      commit_index = LastLogIndex();

    // Is there more data to put in the appendEntries message ?
    for (auto n : current_group){
      Ipv4Address ip = n.second;
      if(GetIpAddress() != ip){
        int next_index = max(next_indexes[n.first], LastCommitConfIndex()+1);
        entries.prev_log_index = next_index - 1;
        //          entries.prev_log_index = next_indexes[n.first] - 1;
        entries.prev_log_term = GetLogTerm(entries.prev_log_index);
        string payload((char*)&entries, sizeof(entries));

        if (next_index <= LastLogIndex()){
          if (mempool.count(log[next_index].second) > 0){
            //               The entry is a block to send
            entries.msg_type = APPEND_ENTRY;
            entries.entry_term = GetEntry(next_index).first;
            debug_suffix << "Send a block (term: " << entries.entry_term << ") to Node " << n.first << ": " <<
              mempool[log[next_index].second];
            debug(debug_suffix.str());
            payload = string((char*)&entries, sizeof(entries));
            payload = payload + mempool[log[next_index].second].Serialize();
            msg_type = "BLOCK";
          }
          else if (groups.count(log[next_index].second) > 0){
            entries.msg_type = APPEND_ENTRY_CONF;
            entries.entry_term = GetEntry(next_index).first;
            // The entry is a new configuration to send
            debug_suffix << "Send a configuration change to Node " << n.first << " with" <<
              log[next_index].second;
            debug(debug_suffix.str());
            payload = string((char*)&entries, sizeof(entries));
            payload = payload + SerializeGroup(log[next_index].second, groups[log[next_index].second]);
            msg_type = "APPEND_ENTRY_CONF";
          }
        }
        else{
          entries.msg_type = APPEND_ENTRY;
          payload = string((char*)&entries, sizeof(entries));
          debug_suffix << "Send an heartbeat to Node " << n.first;
          debug(debug_suffix.str());
          msg_type = "HEARTBEAT";
        }

        //debug_suffix << "Send an entry packet of size " << payload.size();
        //debug(debug_suffix.str());
        ApplicationPacket pkt(ApplicationPacket::RAFT, payload);
        SendPacket(pkt, ip, false, msg_type);
      }
    }

    bool check_conf_commit = true;
    if (commit_index == LastLogIndex() && pending_groups.size() > 0){
      string new_group = GetCurrentGroup();
      ApplyGroup(new_group);
      if (pending_groups[0].first == new_group && check_conf_commit)
        pending_groups.erase(pending_groups.begin());
      if (pending_groups.size() == 0){ // We committed the last pending group
        debug_suffix << "Become follower after committing last pending group";
        debug(debug_suffix.str());
        current_state = FOLLOWER;
        traces->EndConfigChange(Simulator::Now().GetSeconds(), node->GetId());
      }
    }
    Simulator::Schedule(Seconds(heartbeat_freq), &B4MeshRaft::SendHeartbeats, this);
  }
}

B4MeshRaft::append_entries_ack_t B4MeshRaft::ProcessAppendEntries(string data_payload){
  ResetElectionTimeout();
  traces->ResetStartElection();
  append_entries_ack_t ret;
  ret.msg_type = ACK_ENTRY;
  ret.term = current_term;
  ret.id = node->GetId();
  ret.commit_index = commit_index;
  ret.entry_index = LastLogIndex();
  ret.success = false;
  append_entries_hdr_t entries = *((append_entries_hdr_t*) data_payload.data());
  string log_entries = data_payload.substr(sizeof(append_entries_hdr_t));

  if (GetCurrentLeader() == -1)
    SetCurrentLeader(entries.leader_id);


  if (entries.term > current_term && entries.leader_commit > commit_index){
    current_state = FOLLOWER;
    SetCurrentLeader(entries.leader_id);
    current_term = entries.term;
  }

  // Prevent from applying a log if leader term is earlier than local term
  if (entries.term < current_term){
    debug_suffix << "Senders term lower";
    debug(debug_suffix.str());
    ret.success = false;
    return ret;
  }

  // Prevent from modifying a commited log
  if (entries.prev_log_index <= commit_index-1){
    debug_suffix << "Modification of committed entry not allowed";
    debug(debug_suffix.str());
    ret.success = false;
    return ret;
  }

  // Destitution of a leader or a candidate if another leader is detected (maybe
  // useless
  if (current_state == LEADER || current_state == CANDIDATE){
    debug_suffix << "Revoked by Node " << entries.leader_id;
    debug(debug_suffix.str());
    RemoveUncommittedEntries();
    current_state = FOLLOWER;
  }

  // Detect log discontinuity and remove uncommitted log entries.
  if (GetEntry(LastLogIndex()).first != entries.term &&
      LastLogIndex() > commit_index &&
      commit_index < entries.prev_log_index &&
      groups.count(GetEntry(commit_index).second) &&
      log_entries.size() > 0){
    debug_suffix << GetEntry(LastLogIndex()).first << " " << entries.term;
    debug(debug_suffix.str());
    debug_suffix << LastLogIndex() << " " << commit_index;
    debug(debug_suffix.str());
    debug_suffix << GetEntry(commit_index).second;
    debug(debug_suffix.str());
    debug_suffix << "Discontinuity with the log after configuration change";
    debug(debug_suffix.str());
    debug_suffix << "Removed " << RemoveUncommittedEntries() << " entries";
    debug(debug_suffix.str());
  }

  // Local log is not up to date
  if (GetEntry(entries.prev_log_index).first != entries.prev_log_term &&
      (groups.count(GetEntry(LastLogIndex()).second) <= 0 /* &&
                                                             entries.msg_type != APPEND_ENTRY_CONF*/)){
    debug_suffix << GetEntry(entries.prev_log_index).first << " / " <<
      entries.prev_log_term << " - " <<
      groups.count(GetEntry(LastLogIndex()).second);
    debug(debug_suffix.str());
    debug_suffix << "log last index " << LastLogIndex();
    debug(debug_suffix.str());
    debug_suffix << "log commit index " << commit_index;
    debug(debug_suffix.str());
    debug_suffix << GetEntry(commit_index).second;
    debug(debug_suffix.str());
    debug_suffix << "\tLog not up to date";
    debug(debug_suffix.str());
    PrintLog();
    ret.success = false;
    return ret;
  }

  // Process entries if there is ones
  debug_suffix << "Process entry of size " << data_payload.size() << "B";
  debug(debug_suffix.str());
  //  string log_entries = data_payload.substr(sizeof(append_entries_hdr_t));
  if (log_entries.size() > 0){
    debug_suffix << "There is " << log_entries.size() << "B to add in the log";
    debug(debug_suffix.str());
    // Add jamming if necessary
    if (log.size() > 0 && groups.count(log[LastLogIndex()].second) > 0){
      for (int i=LastLogIndex(); i<entries.prev_log_index; ++i){
        string hash = GetCurrentGroup();
        log.push_back(make_pair(current_term, hash));
      }
    }
    if (entries.msg_type == APPEND_ENTRY){
      Block b(log_entries);
      debug_suffix << "Received a block : " << b;
      debug(debug_suffix.str());
      if (entries.prev_log_index == LastLogIndex()){
        // New entry
        InsertEntry(b, entries.entry_term);
        debug_suffix << "New entry added during term " << entries.entry_term << " / " << current_term;
        debug(debug_suffix.str());
      }
      else if (entries.prev_log_index < LastLogIndex()){
        // Replace an existing entry
        debug_suffix << "Replaced bloc at index " << entries.prev_log_index+1 << " during term " << entries.term;
        debug(debug_suffix.str());
        log[entries.prev_log_index+1] = make_pair(entries.entry_term, b.GetHash());
        mempool[b.GetHash()] = b;
      }
      else{
        ret.success = false;
        return ret;
      }
    }
    else if (entries.msg_type == APPEND_ENTRY_CONF){
      debug_suffix << "There is a new conf of " << log_entries.size() << "B to add in the log";
      debug(debug_suffix.str());
      pair<string, vector<pair<int, Ipv4Address>>> new_group = DeserializeGroup(log_entries);
      debug_suffix << "Group : " << new_group.first;
      debug(debug_suffix.str());
      if (entries.prev_log_index == LastLogIndex()){
        debug("Add group");
        log.push_back(make_pair(entries.entry_term, new_group.first));
      }
      else{
        log[entries.prev_log_index+1] = make_pair(entries.entry_term, new_group.first);
        debug_suffix << "\t Update the configuration at index " <<
          entries.prev_log_index+1;
        debug(debug_suffix.str());
      }
      groups[new_group.first] = new_group.second;
    }
  }
  else{
    debug("Received an heartbeat");
  }

  // Update commit index
  if (entries.leader_commit > commit_index){
    commit_index = min(entries.leader_commit, LastLogIndex());
    if (pending_groups.size() > 0){
      string new_group = GetCurrentGroup();
      ApplyGroup(GetCurrentGroup());
      if (new_group == pending_groups[0].first){
        pending_groups.erase(pending_groups.begin());
      }
      if (pending_groups.size() == 0){
        traces->EndConfigChange(Simulator::Now().GetSeconds(), node->GetId());
      }
    }
  }

  ret.commit_index = commit_index;
  ret.entry_index = LastLogIndex();
  ret.success = true;

  return ret;
}

void B4MeshRaft::ProcessEntriesAck(append_entries_ack_t ack_entries){
  if (!running) return;
  ResetElectionTimeout();
  if (current_state != LEADER) return;
  if (ack_entries.term > current_term){
    //       Update current_term
    debug("Destituted by receiving an ack with high term");
    RemoveUncommittedEntries();
    current_term = ack_entries.term;
    commit_index = max(commit_index, min(ack_entries.commit_index, LastLogIndex())); // Remove uncommited entries if there is ones.
    current_state = FOLLOWER;
    return;
  }

  if (ack_entries.commit_index > commit_index){
    //           Update current_term
    debug("Destituted by receiving an ack with high commit_index");
    RemoveUncommittedEntries();
    current_term = ack_entries.term;
    commit_index = max(commit_index, min(ack_entries.commit_index, LastLogIndex())); // Remove uncommited entries if there is ones.
    current_state = FOLLOWER;
    return;
  }
  // Is it good to not have this condition
  //  if (ack_entries.success){
  match_indexes[ack_entries.id] = ack_entries.entry_index;
  next_indexes[ack_entries.id] = ack_entries.entry_index+1;
  commit_indexes[ack_entries.id] = ack_entries.commit_index;
  //  }

  // Update commit index
  for (int commit_candidate=commit_index+1; commit_candidate<=LastLogIndex(); ++commit_candidate){
    int count = 0;
    for (auto n : current_group){
      int commit = match_indexes[n.first];
      if (commit >= commit_candidate)
        count++;
    }
    if (groups.count(GetEntry(commit_candidate).second) ||
        groups.count(GetEntry(commit_candidate-1).second)){
      debug_suffix << commit_candidate << " / " << LastLogIndex() << " group " <<
        log[commit_candidate].second << " - " << count;
      debug(debug_suffix.str());
      if ((uint32_t)count == current_group.size()){ // We want unanimity to commit config
        // change entries or first entry after
        // config change
        debug_suffix <<"Increment commit index from " << commit_index << " to ";
        commit_index = min(commit_candidate, LastLogIndex());
        debug_suffix << commit_index;
        debug(debug_suffix.str());
        PrintLog();
      }
    }
    else if ((uint32_t)count > current_group.size() / 2){
      debug_suffix << "Increment commit index from " << commit_index << " to ";
      commit_index = min(commit_candidate, LastLogIndex());;
      debug_suffix << commit_index;
      debug(debug_suffix.str());
      PrintLog();
    }
  }
  commit_indexes[node->GetId()] = commit_index;
}

pair<int, string> B4MeshRaft::GetEntry(int index){
  if (index >= 0 && (uint32_t)index < log.size())
    return log[index];
  else{
    return make_pair(-1, "");
  }
}

bool B4MeshRaft::IsLeader(){
  if (pending_groups.size() > 0) return false;
  return current_state == LEADER;
}

void B4MeshRaft::InsertEntry(Block& b, int term){
//  if (pending_groups.size() > 0) return; // To see if it doesn't cause new
// bugs
  if (term == -1) term = current_term;
  if (mempool.count(b.GetHash()) == 0 || true){
    log.push_back(make_pair(term, b.GetHash()));
    mempool[b.GetHash()] = b;
    if (IsLeader()){
      debug_suffix << "Add new entry " << b;
      debug(debug_suffix.str());
      match_indexes[node->GetId()] = LastLogIndex();
      next_indexes[node->GetId()] = LastLogIndex()+1;
    }
    else{
    }
  }
  else{
    debug("New entry not added");
  }
}

vector<Block> B4MeshRaft::RetrieveBlocks(){
  vector<Block> ret;
  // Retrieve committed blocks
  for (int i = 0; i<=commit_index; ++i){
    if (mempool.count(log[i].second) > 0){
      ret.push_back(mempool[log[i].second]);
    }
  }

  // Remove retrieved blocks from mempool
  for (auto &b : ret){
    mempool.erase(b.GetHash());
  }
  last_applied = commit_index;

  return ret;
}

int B4MeshRaft::GetLogTerm(int index){
  if (index >= 0 && (uint32_t)index < log.size())
    return log[index].first;
  else
    return -1;
}

float B4MeshRaft::GetMajority(){
  return majority;
}
void B4MeshRaft::SetMajority(float m){
  majority = m;
}

void B4MeshRaft::ChangeGroup(pair<string, vector<pair<int, Ipv4Address>>> new_group){
  debug_suffix << "Trigger a group change " << new_group.first << " : ( ";
  for (auto n : new_group.second)
    debug_suffix << n.first << ", ";
  debug_suffix << ")";
  debug(debug_suffix.str());
  if (new_group.second == current_group){
    debug("Already in this group");
    return;
  }
//  cout << "Start Config" << endl;
  traces->ResetStartConfigChange();
  traces->StartConfigChange(Simulator::Now().GetSeconds(), node->GetId());
  pending_groups.clear();
  auto group_sequence = MakeGroupChangeSequence(new_group);
  for (auto group : group_sequence){
    pending_groups.push_back(group);
    groups.insert(make_pair(group.first, group.second));
    if (current_state == CANDIDATE && DetectGroupChange(group.second) == SPLIT){
      ApplyGroup(group.first);
      current_state = FOLLOWER;
    }
  }
}

void B4MeshRaft::ApplyGroup(string group_hash){
  if (groups.count(group_hash) > 0){
    if (current_group == groups[group_hash]) return;
    debug_suffix << "Apply group " << group_hash;
    debug(debug_suffix.str());
    current_group = groups[group_hash];
    majority = current_group.size()/2;
  }
}

string B4MeshRaft::GetCurrentGroup(){
  for (int i=commit_index; i>=0; --i){
    if (groups.count(log[i].second) > 0)
      return log[i].second;
  }
  return "";
}

string B4MeshRaft::SerializeGroup(string group_hash, vector<pair<int, Ipv4Address>> group){
  string ret = group_hash;

  for (auto n : group){
    int nodeId = n.first;
    ret += string((char*) &nodeId, sizeof(int));
  }

  return ret;
}
pair<string, vector<pair<int, Ipv4Address>>> B4MeshRaft::DeserializeGroup(string serie){
  pair<string, vector<pair<int, Ipv4Address>>> ret;

  ret.first = serie.substr(0, 32);

  serie = serie.substr(32);
  while (serie.size() > 0){
    int nodeId = *((int*)serie.data());
    Ptr<Ipv4> ipv4 = ns3::NodeList::GetNode(nodeId)->GetObject<Ipv4>();
    Ipv4Address ip = ipv4->GetAddress(1, 0).GetLocal();
    ret.second.push_back(make_pair(nodeId, ip));
    serie = serie.substr(sizeof(int));
  }
  return ret;
}

void B4MeshRaft::GenerateBlock(){
  if (!running) return;
  if (current_state != LEADER) return;

  if (pending_groups.size() == 0){
    int chance_draw = uniform_rand(0,100);
    if (chance_draw > 50){
      debug("Generate a new block");
      Block b;
      InsertEntry(b);
    }
    Simulator::Schedule(Seconds(5), &B4MeshRaft::GenerateBlock, this);
  }
  else
  {
    debug("Has not generated a new block");
  }
}

vector<pair<string, vector<pair<int, Ipv4Address>>>>
B4MeshRaft::MakeGroupChangeSequence(pair<string, vector<pair<int, Ipv4Address>>>
    new_group){
  vector<pair<string, vector<pair<int, Ipv4Address>>>> ret;

  vector<pair<int, Ipv4Address>> new_nodes = new_group.second;

  vector<pair<int, Ipv4Address>> common_nodes;

  for (auto n : new_nodes)
    if (find(current_group.begin(), current_group.end(), n) != current_group.end())
      common_nodes.push_back(n);


  if (common_nodes.size() == current_group.size()) {// This is a merge
    ret.push_back(new_group);
  } else if (common_nodes.size() == new_group.second.size()) {// This is a split
    ret.push_back(new_group);
  } else {
    // This is a compound config change
    // A split have to be commit first
    string hash = "";
    for (auto n : common_nodes)
      hash = hash + to_string(n.first);
    hash = hash + string(32 - hash.size(), '-');
    ret.push_back(make_pair(hash,common_nodes));
    ret.push_back(new_group);
  }
  return ret;
}

int B4MeshRaft::DetectGroupChange(vector<pair<int, Ipv4Address>> group){

  if (group == current_group) return NONE;

  int common_count = 0;
  for (auto g : group){
    if (find(current_group.begin(), current_group.end(), g) !=
        current_group.end()) common_count++;
  }

  if ((uint32_t)common_count == group.size()) return SPLIT;
  if ((uint32_t)common_count == current_group.size()) return MERGE;
  else return ARBITRARY;
}

bool B4MeshRaft::IsInCurrentGroup(Ipv4Address ip){

  for (auto g : current_group)
    if (g.second == ip) return true;
  return false;
}

int B4MeshRaft::LastCommitConfIndex(){
  for (int i=commit_index; i>=0; --i){
    if (groups.count(log[i].second) > 0) return i;
  }

  return -1;
}

int B4MeshRaft::GetNodeId(Ipv4Address ip){
  for (uint32_t i=0; i<peers.size(); ++i)
    if (peers[i] == ip) return i;
  return 0;
}

int B4MeshRaft::LastLogIndex(){
  if (log.size() == 0){
    return -1;
  }
  else
    return log.size()-1;
}

int B4MeshRaft::RemoveUncommittedEntries(){
  int ret = 0;
  while (commit_index < LastLogIndex()){
    debug_suffix << "Removing last uncommitted entry " << LastLogIndex();
    debug(debug_suffix.str());
    log.pop_back();
    ret++;
  }
  return ret;
}
void B4MeshRaft::PrintLog(){
  for (auto l : log){
    if (groups.count(l.second) > 0)
      debug_suffix << l.second << ", ";
    else
      debug_suffix << dump(l.second.data(), 10) << ", ";
  }
  debug_suffix << "]";
  debug(debug_suffix.str());
}

void B4MeshRaft::debug(string suffix){
/*  cout << Simulator::Now().GetSeconds() << "s: B4MeshRaft : Node " << node->GetId() <<
      " : " << suffix << endl;
  debug_suffix.str("");
*/}
