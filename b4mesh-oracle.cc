#include "b4mesh-oracle.h"

NS_LOG_COMPONENT_DEFINE("B4MeshOracle");
NS_OBJECT_ENSURE_REGISTERED(B4MeshOracle);

TypeId B4MeshOracle::GetTypeId(){
	static TypeId tid = TypeId("B4MeshOracle")
		.SetParent<Application>()
		.SetGroupName("Application")
		.AddConstructor<B4MeshOracle>()
		;
	return tid;
}

vector<void *> B4MeshOracle::nodesB4mesh;     // b4mesh references
// double B4MeshOracle::sendServiceTime;       // milliseconds
// double B4MeshOracle::leaderElectionDelay;   // seconds
// double B4MeshOracle::blockCommitDelay;      // milliseconds


B4MeshOracle::B4MeshOracle(){

	// // Service Times
	// sendServiceTime = 100;      // milliseconds
	// leaderElectionDelay = 2;   // seconds
	// blockCommitDelay = 50;      // milliseconds

	// // Probabilities
	// blockRecvProbability = 1.0;
	// blockCommitProbability = 1.0;
}

B4MeshOracle::~B4MeshOracle(){
}

void B4MeshOracle::SetUp(Ptr<Node> node, vector<Ipv4Address> peers){
	current_state = FOLLOWER;
	//current_leader = -1; //unused?
	running = false;
	current_term = 0;

	recv_sock = 0;

	commit_index = -1;
	last_applied = -1;
	gathered_votes = 0;
	majority = 0;

	heartbeat_freq = 1; // In seconds
	election_timeout_parameters = make_pair(8000, 10000); //was 4000, 6000.
	election_timeout_event = EventId();

	// Initialize trace variables
	start_election = -1;
	end_election = -1;
	received_bytes = make_pair(-1.0, -1);
	sent_bytes = make_pair(-1.0, -1);
	received_messages = make_pair(-1.0, -1);
	sent_messages = make_pair(-1.0, -1);

	sentPacketSizeTotalConsensus = 0;

	this->peers = peers;
	this->node = node;

	// Build initial current group
	for (uint32_t i=0; i<ns3::NodeList::GetNNodes(); ++i)
		current_group.push_back(make_pair(i, GetIpAddress(i)));

	// Set up the receiving socke
	//if (recv_sock == 0){
	if (!recv_sock){
		// Open the receiving socket
		TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
		recv_sock = Socket::CreateSocket(node, tid);
	}

	InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(),90);
	recv_sock->Bind(local);
	recv_sock->SetRecvCallback (MakeCallback (&B4MeshOracle::ReceivePacket, this));

}

void B4MeshOracle::ReceivePacket(Ptr<Socket> socket){
	Ptr<Packet> packet;
	Address from;
	while ((packet = socket->RecvFrom(from))){
		if (InetSocketAddress::IsMatchingType(from)){
			InetSocketAddress iaddr = InetSocketAddress::ConvertFrom(from);
			Ipv4Address ip = iaddr.GetIpv4();

			//debug_suffix.str("");
			//debug_suffix << " Received Packet : New packet of " << packet->GetSize() << "B from Node " << ip;
			//debug(debug_suffix.str());

			string parsedPacket;
			//NS_LOG_UNCOND("packet size is " << packet->GetSize());
			if (packet->GetSize() == 0) {
				//NS_LOG_UNCOND("packet size = 0 issue" << packet->GetSize());
				return;
			}
			char* packetInfo = new char[packet->GetSize()];
			packet->CopyData(reinterpret_cast<uint8_t*>(packetInfo), packet->GetSize());
			string parsed_packet = string(packetInfo, packet->GetSize());
			delete[] packetInfo;
			

			try{
				ApplicationPacket p(parsed_packet);
				// For traces propuses
				//b4mesh_throughput[Simulator::Now().GetSeconds()] = p.CalculateSize();
				
				if (p.GetService() == ApplicationPacket::CONSENSUS){
					
					int message_type = ExtractMessageType(p.GetPayload());
					
					if (!IsInCurrentGroup(ip)) {
						debug("Message dropped because not from the same group"); // ? correct?
						return; // Ignore all messages from nodes not in current_group
					}
				
					if (message_type == REQ_VOTE){
						// Follower receiving a vote request from a candidate trying to be leader
						debug_suffix.str("");
						debug_suffix << "Received a request vote " << packet->GetSize() << "from Node " << GetNodeId(ip);

						request_vote_hdr_t req_vote = *((request_vote_hdr_t*)p.GetPayload().data());
						auto ret = ProcessRequestVote(req_vote); 

						ApplicationPacket ret_pkt(ApplicationPacket::CONSENSUS, string((char*)&ret, sizeof(ret)));

						//SendPacket(ret_pkt, ip, false, "ACK_VOTE");
						SendPacket(ret_pkt, ip, false);

					
					} else if (message_type == ACK_VOTE) {
						// Leader receiving a vote acknowledgement from a follower.

						debug_suffix << "Received a request vote response from Node " << GetNodeId(ip);
						debug(debug_suffix.str());
						// cout << Simulator::Now().GetSeconds() << " RECV " << "ACK_VOTE " 
						//		<< node->GetId() <<  " " << GetNodeId(ip) << " " << packet->GetSize() << " " << endl;

						request_vote_ack_t ack_vote = *((request_vote_ack_t*)p.GetPayload().data());

						ProcessVoteResponse(ack_vote);

					} else if (message_type == APPEND_ENTRY || message_type == APPEND_ENTRY_CONF) {
						// Follower receiving an AppendEntry/Config change (within heartbeat msg) from a Leader

						debug_suffix << "Received an append" " entries message of " <<
								packet->GetSize() << "B from Node " << GetNodeId(ip);
						debug(debug_suffix.str());

						//cout << Simulator::Now().GetSeconds() << " RECV " << "APPEND_ENTRY " 
						//		<< node->GetId() <<  " " << GetNodeId(ip) << " " << packet->GetSize() << " " << endl;

						auto ret = ProcessAppendEntries(p.GetPayload());
						ApplicationPacket ret_pkt(ApplicationPacket::CONSENSUS, string((char*)&ret, sizeof(ret)));

						//SendPacket(ret_pkt, ip, false, "ACK_ENTRY");
						SendPacket(ret_pkt, ip, false);

					} else if (message_type == ACK_ENTRY) {
						// Leader receiving acknowledgement of AppendEntry from Follower	
						debug_suffix << "Received an ack entries message of " <<
							packet->GetSize() << "B from Node " << GetNodeId(ip);
						debug(debug_suffix.str());

						//cout << Simulator::Now().GetSeconds() << " RECV " << "ACK_ENTRY " 
						//		<< node->GetId() <<  " " << GetNodeId(ip) << " " << packet->GetSize() << " " << endl;

						append_entries_ack_t entries_ack = *((append_entries_ack_t*)p.GetPayload().data());
						ProcessEntriesAck(entries_ack);
					} 
				}
			} catch (const std::exception& e){
				//pass
				NS_LOG_UNCOND("Exception parsing packet");
			}
		}
	}
}

void B4MeshOracle::SendPacket(ApplicationPacket& packet, Ipv4Address ip, bool scheduled){

	if (running == false){
    	return;
	}
	

	// // I don't really understand this still.
	// if (!scheduled){
	// 	float desync = (rand() % 100) / 1000.0;
	// 	Simulator::Schedule(Seconds(desync),
    //     	&B4MeshOracle::SendPacket, this, packet, ip, true);
    // 	return;
	// }
    // Create the packet to send
  	Ptr<Packet> pkt = Create<Packet>((const uint8_t*)(packet.Serialize().data()), packet.GetSize());

	// Traces: Add by Ethan
	sentPacketSizeTotalConsensus += pkt->GetSize();

	// Open the sending socket
	TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
	Ptr<Socket> source = Socket::CreateSocket (node, tid);
	InetSocketAddress remote = InetSocketAddress (ip, 90);
	source->Connect(remote);

	int res = source->Send(pkt);
	source->Close();

	if (res > 0){
    	/*
    	debug_suffix.str("");
    	debug_suffix << GetIpAddress() << " sends a packet of size " << pkt->GetSize() << " to " << ip << endl;
    	debug(debug_suffix.str());
    	*/
	} else {
		debug_suffix.str("");
		debug_suffix << GetIpAddress() << " failed to send a packet of size " << pkt->GetSize() << " to " << ip << endl;
		debug(debug_suffix.str());
  	}
}

Ipv4Address B4MeshOracle::GetIpAddress(){
	Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
	return ipv4->GetAddress(1, 0).GetLocal();
}

// Overloaded method
Ipv4Address B4MeshOracle::GetIpAddress(int i){
	Ptr<Ipv4> ipv4 = ns3::NodeList::GetNode(i)->GetObject<Ipv4>();
	return ipv4->GetAddress(1, 0).GetLocal();
}

void B4MeshOracle::StartApplication(){
	running = true;
	ResetElectionTimeout();
}

void B4MeshOracle::StopApplication(){
	running = false;
}

vector<int> B4MeshOracle::GetNextIndexes(){
	return next_indexes;
}

vector<int> B4MeshOracle::GetMatchIndexes(){
	return match_indexes;
}

int B4MeshOracle::GetCommitIndex(){
	return commit_index;
}

int B4MeshOracle::GetLastApplied(){
	return last_applied;
}

vector<pair<int,string>>& B4MeshOracle::GetLog(){
	return log;
}

int B4MeshOracle::GetCurrentLeader(){
	return current_leader;
}

void B4MeshOracle::SetCurrentLeader(int ld){
	current_leader = ld;
}

int B4MeshOracle::GetVotedFor(){
  return voted_for;
}

int B4MeshOracle::GetCurrentTerm(){
  return current_term;
}

int B4MeshOracle::GetCurrentState(){
	return current_state;
}

void B4MeshOracle::SetCurrentState(int st) {
	current_state = st;
}

bool B4MeshOracle::IsLeader(){
	return current_state == LEADER;
}

// KIV to fix
void B4MeshOracle::ResetElectionTimeout(float factor){
	if (!running) return;
	if (election_timeout_event != EventId()){
		Simulator::Cancel(election_timeout_event);
	}

	float delay = uniform_rand(election_timeout_parameters.first,
	election_timeout_parameters.second) / 1000.;

	delay = delay * factor;
	election_timeout_event = Simulator::Schedule(Seconds(delay), &B4MeshOracle::TriggerElectionTimeout, this);
	//TriggerElectionTimeout();
}

// When timeout runs out,
// Become a candidate and vote for self. 
// Request for votes.
void B4MeshOracle::TriggerElectionTimeout(){
	debug("Election timeout triggered. Becoming candidate and voting for self.");
	if (!running) return;
	SetCurrentLeader(-1);
	ResetElectionTimeout();

	if (current_state == CANDIDATE){ // Avoid yoyo effect between term and log advancement (?)
		current_term--;
	}

	if (voted_for != -1) { // Avoid vote locking for candidate that timeout(?)
		gathered_votes = 0;
		voted_for = -1;
		current_state = FOLLOWER;
		return;
	}

	// If it is a split, apply first pending group
	// Pending groups = { [group_hash, [group] ], ...}
	// Apply group: set current_group to the group associated with group_hash and update majority


	//NS_LOG_UNCOND("pending_groups size: " << pending_groups.size());
	// (not entirely sure): because only update the majority criterion for a split, not for a merge
	if (pending_groups.size() > 0){
		string new_group_hash = pending_groups[0].first;
		vector<pair<int, Ipv4Address>> new_group = pending_groups[0].second;
		if (DetectGroupChange(new_group) == SPLIT) {
			ApplyGroup(new_group_hash);
		}
	}

	// Initiate election process
	current_term++;
	current_state = CANDIDATE;
	voted_for = node->GetId(); // Vote for itself
	gathered_votes = 1;

	// Create request Vote packet
	request_vote_hdr_t req_vote;
	req_vote.msg_type = REQ_VOTE;
	req_vote.term = current_term;
	req_vote.candidate_id = node->GetId();
	req_vote.last_log_index = LastLogIndex(); // Replaced by last commit (?)

	//NS_LOG_UNCOND("commit_index: " << commit_index);
	if (commit_index >= 0 && !log.empty()) { // Had to add the !log.empty(). But this doesn't make sense also. if commit_index>
		//req_vote.last_log_term = log.back.first();
		req_vote.last_log_term = log.back().first;
	} else {
		req_vote.last_log_term = -1;
	}

	ApplicationPacket pkt(ApplicationPacket::CONSENSUS, string((char*)&req_vote, sizeof(req_vote)));

	// Broadcast this vote request to all nodes in current_group
	BroadcastPacket(pkt);

	// for (auto& n : current_group) {
	// 	Ipv4Address ip = n.second();
	// 	if (GetIpAddress() ! = ip) { // Don't send to self
	// 		SendPacket(pkt, ip, false, "REQ_VOTE");
	// 	}
	// }

	// Only node in the group, so just make it the leader
	//NS_LOG_UNCOND("current_group size: " << current_group.size());
	if (current_group.size() == 1){
		current_state = LEADER;
		SetCurrentLeader(node->GetId());
		// end_election = Simulator::Now().GetSeconds();
		// traces->EndElection(end_election, node->GetId());
		// traces->ResetStartElection();
		next_indexes = vector<int>(peers.size(), commit_index+1);
		match_indexes = vector<int>(peers.size(), 0);// was originally 0?
		commit_indexes = vector<int>(peers.size(), -1);
		SendHeartbeats();
	}
}

// void B4MeshOracle::ChangeGroup(pair<string, vector<pair<int, Ipv4Address>>> new_group, int natchange){

// 	// Upon receiving a group change:
// 	// Decide whether to run a leader election

// 	if (new_group.second.size() == 1) {
// 		current_group = new_group.second;
// 		current_leader = node->GetId();
// 		current_state = LEADER;
// 		return;
// 	}

// 	// If it is a split
// 	if (natchange == SPLIT) {

// 		//Set the new group as the current group
// 		current_group = new_group.second;

// 		// if the node is the leader, it does trigger a leader election
// 		if (!IsLeader()) {
// 			if (GetCurrentLeader() != -1) {  // if there is a leader
// 				if (!IsInCurrentGroup(GetIpAddress(GetCurrentLeader()))) {
// 					StartElection();
// 				}
// 			} else {
// 				StartElection();
// 			}
// 		}
// 	} else if (natchange == MERGE || natchange == ARBITRARY) {
// 		current_group = new_group.second;
// 		StartElection();
// 	}

// 	SetMajority();
// 	traces->ResetStartConfigChange();
// 	traces->StartConfigChange(Simulator::Now().GetSeconds());

// }

// void B4MeshOracle::StartElection() {

// 	term++;
// 	current_leader = -1;
// 	if (current_state == LEADER)
// 		current_state = FOLLOWER;

// 		// Calculate election delay:
// 		//    Sending request for votes delay +
// 		//    Receiving votes delays
// 	Simulator::Schedule(Seconds(leaderElectionDelay),
// 			&B4MeshOracle::EndElection, this, term);
// }

// void B4MeshOracle::EndElection(uint32_t t_elect){

// /* Disable
// 	double minSize = 0;                   // pick a random leader
// 	double maxSize = current_group.size();
// 	Ptr<UniformRandomVariable> randomLeader = CreateObject<UniformRandomVariable> ();
// 	randomLeader->SetAttribute("Min", DoubleValue(minSize));
// 	randomLeader->SetAttribute("Max", DoubleValue(maxSize));
// 	int pos = trunc(randomLeader->GetValue());
// */
// 	if (term == t_elect) {
// 		unsigned int winner = current_group.at(0).first;
// 		current_leader = winner;     // the first node of the group is always the leader
// 		if (node->GetId() == winner) {
// 			SetCurrentState(LEADER);
// 		}

// 		// Indicate b4mesh graph that we have a leaders
// 		indicateNewLeader(nodesB4mesh.at(node->GetId()));
// 	}
// }

// Broadcast packet to all nodes in the current_group
void B4MeshOracle::BroadcastPacket(ApplicationPacket& packet){
	for (auto& n : current_group){
		Ipv4Address ip = n.second;
		if(GetIpAddress() != ip)
			SendPacket(packet, ip, false);
	}
}


int B4MeshOracle::ExtractMessageType(const string& msg_payload){
	int ret = *((int*)msg_payload.data());
	return ret;
}

// Called by a voters responding to a vote request by a candidate.
B4MeshOracle::request_vote_ack_t B4MeshOracle::ProcessRequestVote(request_vote_hdr_t msg){
	ResetElectionTimeout();
	request_vote_ack_t ret;
	ret.msg_type = ACK_VOTE;
	ret.term = current_term;
	ret.commit_index = commit_index;
	ret.vote_granted = 0;

  	// David: Should it be less or equal to current_term
  	// What happen if msg.term == current_term ?
	if (msg.term <= current_term) { //Changed to this
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
	} else {
		debug("Refuse vote");
		ret.vote_granted = 0;
	}

	return ret;
}

// Unused?
int B4MeshOracle::GetLastLogTerm(){
	if (LastLogIndex() >= 0) {
		return log.back().first;
	} else {
		return -1;
	}
}

// Called by candidate processing a vote response from a voter.
// If enough votes to be leader, become leader then start sending heartbeats.
void B4MeshOracle::ProcessVoteResponse(request_vote_ack_t ack_vote){
	if (!running) return;
	ResetElectionTimeout();
	if (current_state == FOLLOWER) return; // This should only be called by leader.

	if (ack_vote.term > current_term || ack_vote.commit_index > commit_index){
		// Update current_term
		current_term = ack_vote.term;
		current_state = FOLLOWER;
  	}

	if (current_state == FOLLOWER){
		debug("Destituted from candidate or leader");
		RemoveUncommittedEntries();
		return;
  	}

  	if (current_state == LEADER) {
		return;
	}


	// If node's current_state == CANDIDATE:
  	if (ack_vote.vote_granted){
		gathered_votes++;
	}

	debug_suffix << "Gathered " << gathered_votes << " votes";
  	debug(debug_suffix.str());	
		
	if ((uint32_t) gathered_votes > current_group.size() / 2) { 
		debug_suffix << "Become leader from a majority of " << gathered_votes << " votes";
		debug(debug_suffix.str());
		current_state = LEADER;
		SetCurrentLeader(node->GetId());
		indicateNewLeader(nodesB4mesh.at(node->GetId())); //ADDED THIS!
		
		end_election = Simulator::Now().GetSeconds();
		//traces->EndElection(end_election, node->GetId());
		
		next_indexes = vector<int>(peers.size(), commit_index+1);
		match_indexes = vector<int>(peers.size(), 0); // was 0 at first
		commit_indexes = vector<int>(peers.size(), 0);
		
		SendHeartbeats(); // Haven't implemented yet
		//GenerateBlock(); //Does b4mesh need to be notified of leader change??
  	}
}

// Called by a leader.
// If contains nothing, is just a heartbeat to let followers know it's alive.
// Can also contain a block or config change entry to send to followers as AppendEntry.
// This function also handles the topo change partially,
// If there are pending config changes inside pending_groups, Apply it if is a split. (majority criteria changes too)
// Then if all previous entries have been committed, add the config change entry to the log.
// At the end, Apply the group from the most recent config change entry in the log. 
// Remove from the pending groups list and become follower if applicable
void B4MeshOracle::SendHeartbeats(){
	if (!running) return;

	// Only leader should be sending heartbeats
	if (current_state != LEADER) {
		return;
	}

	append_entries_hdr_t entries;
	entries.msg_type = APPEND_ENTRY;
	entries.term = current_term;
	entries.leader_id = node->GetId();
	entries.prev_log_index = LastLogIndex();
	entries.prev_log_term = GetLogTerm(LastLogIndex());
	entries.leader_commit = commit_index;
	entries.entry_term = -1;

	string msg_type;

	// pending_groups contains [group hash, group] of Config Change entries that have not yet been committed to the log
	// Leader's job is to commit these.

	// Apply first pending group if it is a split
	// I think this is because, the rules for changing of "majority criterion" are: majority criterion based on new group for split
	// but based on old group for merge. So can only apply the new group if it is a split.
	if (pending_groups.size() > 0){
		if (DetectGroupChange(pending_groups[0].second) == SPLIT) {
			ApplyGroup(pending_groups[0].first);
		}
	}

	// When there is a pending group, all the previous entries have to be commmitted still.
	// The committing is done by ProcessEntriesAck() and is based on the new majority criterion (see above)
	// Need to wait for all entries to be committed before can add the config change entry.

	// Once commit index has reached last log index, means all logs committed.
	// Safe to add the config change entry into the log
	if (LastLogIndex() == commit_index && pending_groups.size() > 0){
		if (LastLogIndex() == -1 || pending_groups[0].first != log[LastLogIndex()].second){
			debug_suffix << "Add new group (" << pending_groups[0].first << " in log";
			debug(debug_suffix.str());
			log.push_back(make_pair(current_term, pending_groups[0].first));
			match_indexes[node->GetId()] = LastLogIndex(); // Are these necessary?
			next_indexes[node->GetId()] = LastLogIndex()+1; // Shouldn't it be incrementing last_applied?
		}
	}

	// If the node is alone in the group after a split, the entries are committed immediately
	if (current_group.size() == 1){
		commit_index = LastLogIndex();
	}

	// Is there more data to put in the appendEntries message ?
	for (auto n : current_group){
		Ipv4Address ip = n.second;
		if (GetIpAddress() != ip){

			int next_index = max(next_indexes[n.first], LastCommitConfIndex()+1);
			// This line has to do with the log alignment procedure.
			// If some nodes have logs that are "lagging" due to different transaction speeds in diff branches,
			// then need to take the LastCommitConfiguration Index.
			// If not can just proceed normally.

			entries.prev_log_index = next_index - 1;
			//entries.prev_log_index = next_indexes[n.first] - 1;
			
			
			entries.prev_log_term = GetLogTerm(entries.prev_log_index);
			string payload((char*)&entries, sizeof(entries));

			string hash = "";
			if (next_index >= 0 && next_index < log.size()) {
   				hash = log[next_index].second;
			} else {
				debug_suffix << "Problem with hash ";
				debug(debug_suffix.str());
				//continue;
			}
			
			//string hash = log[next_index].second; 
			
			// This is either a block hash or group hash.

			if (next_index <= LastLogIndex() && hash!=""){ // If there is something to send [not just heartbeat], (either a block or config change entry)

				// debug_suffix << "Next index[n.first] is " << next_indexes[n.first];
				// debug(debug_suffix.str());

				// debug_suffix << "LastCommitConfIndex()+1 is " << LastCommitConfIndex()+1;
				// debug(debug_suffix.str());
		
				// debug_suffix << "next_index is " << next_index;
				// debug(debug_suffix.str());

				// debug_suffix << "log.size() is " << log.size();
				// debug(debug_suffix.str());

				if (blockpool.count(hash) > 0){ // The entry is a block to send
					entries.msg_type = APPEND_ENTRY;
					entries.entry_term = GetEntry(next_index).first;
					debug_suffix << "Send a block (term: " << entries.entry_term << 
							") to Node " << n.first << ": " << blockpool[hash];
					debug(debug_suffix.str());
					payload = string((char*)&entries, sizeof(entries));
					payload = payload + blockpool[hash].Serialize(); // Serialise the block and send it
					msg_type = "BLOCK";

				} else if (groups.count(hash) > 0){ // The entry is a new config change entry to send
					entries.msg_type = APPEND_ENTRY_CONF;
					entries.entry_term = GetEntry(next_index).first;
					
					debug_suffix << "Send a configuration change to Node " << n.first << " with" << hash;
					debug(debug_suffix.str());
					payload = string((char*)&entries, sizeof(entries));
					payload = payload + SerializeGroup(hash, groups[hash]);
					msg_type = "APPEND_ENTRY_CONF";
				}
			} else { // Everything to be sent has been sent already.
				entries.msg_type = APPEND_ENTRY;
				payload = string((char*)&entries, sizeof(entries));
				debug_suffix << "Send an heartbeat to Node " << n.first;
				debug(debug_suffix.str());
				msg_type = "HEARTBEAT";
			}

			debug_suffix << "Send an entry packet of size " << payload.size();
			debug(debug_suffix.str());
			ApplicationPacket pkt(ApplicationPacket::CONSENSUS, payload);
			// SendPacket(pkt, ip, false, msg_type);
			SendPacket(pkt, ip, false);
		}
	}

	bool check_conf_commit = true;

	// commit_index at the end of log means everything has been committed, including any config change entries in the log.
	// This means leader can now switch to "stable topology mode", ie. adopting that new group.
	// Then drop to become a follower.
	if (commit_index == LastLogIndex() && pending_groups.size() > 0){ // Committed all logs but have pending group
		// GetCurrentGroup() gives the group from the most recent group change config entry in the local log
		string new_group = GetCurrentGroup();
		ApplyGroup(new_group); // Then set the current group to that group

		if (pending_groups[0].first == new_group && check_conf_commit)
			pending_groups.erase(pending_groups.begin());
		if (pending_groups.size() == 0){ // We committed the last pending group
			debug_suffix << "Become follower after committing last pending group";
			debug(debug_suffix.str());
			current_state = FOLLOWER;
			//traces->EndConfigChange(Simulator::Now().GetSeconds(), node->GetId());
		}
	}		
	Simulator::Schedule(Seconds(heartbeat_freq), &B4MeshOracle::SendHeartbeats, this);
}


// Called by followers when it receives AppendEntry from leader.
// Settle its own log replication.
B4MeshOracle::append_entries_ack_t B4MeshOracle::ProcessAppendEntries(string data_payload) {
	ResetElectionTimeout();
	//traces->ResetStartElection();
	append_entries_ack_t ret;
	ret.msg_type = ACK_ENTRY;
	ret.term = current_term;
	ret.id = node->GetId();
	ret.commit_index = commit_index;
	ret.entry_index = LastLogIndex();
	ret.success = false;
	
	append_entries_hdr_t entries = *((append_entries_hdr_t*) data_payload.data());
	string log_entries = data_payload.substr(sizeof(append_entries_hdr_t));

	if (GetCurrentLeader() == -1) {
		SetCurrentLeader(entries.leader_id);
	}

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
	// Destitution of a leader or a candidate if another leader is detected (maybe useless)
	// if (current_state == LEADER || current_state == CANDIDATE){
	// 	debug_suffix << "Revoked by Node " << entries.leader_id;
	// 	debug(debug_suffix.str());
	// 	RemoveUncommittedEntries();
	// 	current_state = FOLLOWER;
	// }

  	// Detect log discontinuity and remove uncommitted log entries.
  	if (GetEntry(LastLogIndex()).first != entries.term &&
	// if(
		LastLogIndex() > commit_index &&
		commit_index < entries.prev_log_index && //was originally <
		groups.count(GetEntry(commit_index).second) &&
		log_entries.size() > 0)	{
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


		// debug_suffix << "entries.prev_log_index is " << entries.prev_log_index;
		// debug(debug_suffix.str());

		// debug_suffix << "entries.leader_commit is " << entries.leader_commit;
		// debug(debug_suffix.str());

		// debug_suffix << "log size is " << log.size();
		// debug(debug_suffix.str());

		// debug_suffix << "LastLogIndex() is " << LastLogIndex();
		// debug(debug_suffix.str());

		// debug_suffix << "commit_index is " << commit_index;
		// debug(debug_suffix.str());
	}
		
<<<<<<< HEAD
=======

>>>>>>> main
		// debug_suffix << "GetEntry(entries.prev_log_index).first is " << GetEntry(entries.prev_log_index).first;
		// debug(debug_suffix.str());

		// debug_suffix << "entries.prev_log_term " <<  entries.prev_log_term;
		// debug(debug_suffix.str());

  	// Local log is not up to date
  	if (GetEntry(entries.prev_log_index).first != entries.prev_log_term &&
      (groups.count(GetEntry(LastLogIndex()).second) <= 0 /* &&
                                                             entries.msg_type != APPEND_ENTRY_CONF*/)) {
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
    	//PrintLog();

    	ret.success = false;
    	return ret;
	}

	// Process the entries
	debug_suffix << "Process entry of size " << data_payload.size() << "B";
	debug(debug_suffix.str());

	if (log_entries.size() > 0){
		debug_suffix << "There is " << log_entries.size() << "B to add in the log";
		debug(debug_suffix.str());

		//Jam?
		if (log.size() > 0 && groups.count(log[LastLogIndex()].second) > 0){
			for (int i=LastLogIndex(); i<entries.prev_log_index; ++i){
				string hash = GetCurrentGroup();
				log.push_back(make_pair(current_term, hash));
			}
		}

		if (entries.msg_type == APPEND_ENTRY){
			Block b(log_entries);
			debug_suffix << "Received a block : " << b << " with hash " << b.GetHash();
			debug(debug_suffix.str());
			if (entries.prev_log_index == LastLogIndex()){
				// New entry
				AppendLog(b, entries.entry_term); //Replace thise!

				debug_suffix << "New entry added during term " << entries.entry_term << " / " << current_term;
				debug(debug_suffix.str());
			} else if (entries.prev_log_index < LastLogIndex()){
				// Replace an existing entry
				debug_suffix << "Replaced block at index " << entries.prev_log_index+1 << " during term " << entries.term;
				debug(debug_suffix.str());

				log[entries.prev_log_index+1] = make_pair(entries.entry_term, b.GetHash());
				blockpool[b.GetHash()] = b;
			} else{
				debug("Block appending failed somehow");

				ret.success = false;
				return ret;
			}
		} else if (entries.msg_type == APPEND_ENTRY_CONF){
			debug_suffix << "There is a new conf of " << log_entries.size() << "B to add in the log";
			debug(debug_suffix.str());

			pair<string, vector<pair<int, Ipv4Address>>> new_group = DeserializeGroup(log_entries);

			debug_suffix << "Group : " << new_group.first;
			debug(debug_suffix.str());


			if (entries.prev_log_index == LastLogIndex()){
				debug("Add group");
				log.push_back(make_pair(entries.entry_term, new_group.first));
				groups[new_group.first] = new_group.second; // add {[group_hash -> [group]]}
			} else if (entries.prev_log_index < LastLogIndex()) {
				debug("Replace last entry?");
				log[entries.prev_log_index+1] = make_pair(entries.entry_term, new_group.first);
				debug_suffix << "\t Update the configuration at index " <<
				entries.prev_log_index+1;
				debug(debug_suffix.str());
				groups[new_group.first] = new_group.second; // add {[group_hash -> [group]]}
			} else {
				debug("Config chane appending failed somehow");
				ret.success = false;
				return ret;
			}

			
		}
	} else {
		debug("Received an heartbeat");
	}

	// Follower updates local commit index
	// The ACTUAL committing logic should be here too?? 
	
	// Also, logic for finishing the configuration change.
	// Again, GetCurrentGroup() gets the most recent group change config from the log.
	// Then apply it.
	// How does pending_groups relate to followers?
	// KIV: I think pending_groups is updated by ChangeGroup() for ALL nodes not just follower nodes.
	// Because there's no logic from leader->follower to update pending_groups 

	int prev_commit_index = commit_index; // So that I know how many new blocks to send back to Blockgraph module

	if (entries.leader_commit > commit_index){
		commit_index = min(entries.leader_commit, LastLogIndex());
		if (pending_groups.size() > 0){
			string new_group = GetCurrentGroup();
			ApplyGroup(GetCurrentGroup());
			if (new_group == pending_groups[0].first){
				pending_groups.erase(pending_groups.begin());
			}
			if (pending_groups.size() == 0){
				//traces->EndConfigChange(Simulator::Now().GetSeconds(), node->GetId());
			}
		}
	}

	// Actual committing logic here
	for (int idx = prev_commit_index; idx < commit_index; ++idx) {
		int new_committed_entry = idx+1;
		if (new_committed_entry >= 0 && new_committed_entry <= log.size()-1) { // Added this check
			string hash = log[new_committed_entry].second;
			if (blockpool.count(hash) > 0){ // Only want to send actual Blocks to Blockgraph, not Config Change Entries
				Block b = blockpool[hash];
				IndicateBlockCommit(b);
			} else if (groups.count(hash)> 0){
				debug("CONFIG CHANGE ENTRY COMMITTED");
			}
		}

	}
<<<<<<< HEAD
	
=======


>>>>>>> main
	ret.commit_index = commit_index;
	ret.entry_index = LastLogIndex();
	ret.success = true;

	return ret;
}

// Called by leader in response to followers' acknowledgements to AppendEntries.
// Contains leader committing logic (when can something be committed?).
void B4MeshOracle::ProcessEntriesAck(append_entries_ack_t ack_entries){
	if (!running) return;
	ResetElectionTimeout();
	if (current_state != LEADER) return;
	if (ack_entries.term > current_term){
		// Update current_term
		debug("Destituted by receiving an ack with high term");
		RemoveUncommittedEntries();
		current_term = ack_entries.term;
		commit_index = max(commit_index, min(ack_entries.commit_index, LastLogIndex())); // Remove uncommited entries if there is ones.
		current_state = FOLLOWER;
		return;
	}

	if (ack_entries.commit_index > commit_index){
		// Update current_term
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

	// Update leader's local commit index
	// So should the "committing logic" be in place here as well for the leader?
	int prev_commit_index = commit_index;

	debug_suffix << "Current group size is " << current_group.size();
	debug(debug_suffix.str());

	for (int commit_candidate=commit_index+1; commit_candidate<=LastLogIndex(); ++commit_candidate){
		int count = 0;
		// for (size_t i = 0; i < match_indexes.size(); ++i) {
   		// 	std::cout << "match_indexes[" << i << "] = " << match_indexes[i] << std::endl;
		// }
		for (auto n : current_group){
			int commit = match_indexes[n.first];
			if (commit >= commit_candidate)
				count++;
		}

		debug_suffix << "Current count is " << count;
		debug(debug_suffix.str());

		if (groups.count(GetEntry(commit_candidate).second) ||
			groups.count(GetEntry(commit_candidate-1).second)){
			debug_suffix << commit_candidate << " / " << LastLogIndex() << " group " <<
				log[commit_candidate].second << " - " << count;
			debug(debug_suffix.str());
			if ((uint32_t)count == current_group.size()){ // We want unanimity to commit config. WAS ORIGINALLY == size
				// change entries or first entry after
				// config change
				debug_suffix <<"***Increment commit index from " << commit_index << " to ";
				commit_index = min(commit_candidate, LastLogIndex());
				debug_suffix << commit_index;
				debug(debug_suffix.str());
				PrintLog();
			}
		}
		else if ((uint32_t)count > current_group.size() / 2){
			debug_suffix << "***Increment commit index from " << commit_index << " to ";
			commit_index = min(commit_candidate, LastLogIndex());;
			debug_suffix << commit_index;
			debug(debug_suffix.str());
			PrintLog();
		}
	}

	// Actual committing logic here
	for (int idx = prev_commit_index; idx < commit_index; ++idx) {
		int new_committed_entry = idx+1;
		if (new_committed_entry >= 0 && new_committed_entry <= log.size()-1) { // Added this check
			string hash = log[new_committed_entry].second;
			if (blockpool.count(hash) > 0){ // Only want to send actual Blocks to Blockgraph, not Config Change Entries
				Block b = blockpool[hash];
				IndicateBlockCommit(b);
			} else if (groups.count(hash)> 0){
				debug("CONFIG CHANGE ENTRY COMMITTED");
			}
		}

	}
	
	commit_indexes[node->GetId()] = commit_index;
}

pair<int, string> B4MeshOracle::GetEntry(int index){
	if (index >= 0 && (uint32_t)index < log.size())
		return log[index];
	else{
		return make_pair(-1, "");
	}
}


// Given a block and a term,
// Append the entries to the log
// If it is a leader calling this, push back the indexes as well.
// Called when follower receives a block as part of AppendEntryRPC, 
void B4MeshOracle::AppendLog(Block&b, int term) { // Originally called InsertEntry() in c4m

	if (term == 1) {
		term = current_term;
	}

	if (blockpool.count(b.GetHash()) == 0 || true) {
		log.push_back(make_pair(term, b.GetHash()));
		blockpool[b.GetHash()] = b;
		if (IsLeader()){
		 	debug_suffix << "Add new entry " << b;
		 	debug(debug_suffix.str());
		 	match_indexes[node->GetId()] = LastLogIndex(); // Should this be changing last_applied? 
		 	next_indexes[node->GetId()] = LastLogIndex()+1;
		}
	}
}

// Given index of the log, get the term
int B4MeshOracle::GetLogTerm(int index){
	if (index >= 0 && (uint32_t)index < log.size()) {
		return log[index].first;
	} else {
		return -1;
	}
}


float B4MeshOracle::GetMajority() {
	return majority;
}

void B4MeshOracle::SetMajority() {
	majority = current_group.size()/2;
}

// Invoked on ALL(??) nodes by the upper layer node discovery protocol?
// Clear pending_groups -> why??
// Form a new group change sequence with the new group
// Group change sequence: either a simple one element vector (for split/merge), 
// or 2 element vector, split then merge for compound

// Appends the pending_groups list with the new configurations.

void B4MeshOracle::ChangeGroup(pair<string, vector<pair<int, Ipv4Address>>> new_group){
	debug_suffix << "Trigger a group change " << new_group.first << " : ( ";
	for (auto n : new_group.second)
		debug_suffix << n.first << ", ";
	debug_suffix << ")";
	debug(debug_suffix.str());
	
	if (new_group.second == current_group){
		debug("Already in this group");
		return;
	}

	// traces->ResetStartConfigChange();
	// traces->StartConfigChange(Simulator::Now().GetSeconds(), node->GetId());

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

// Set the current_group to the group associated with the group hash (if there is one), 
// Then update majority (take note of this!!! Esp for splits. That's the whole point)
void B4MeshOracle::ApplyGroup(string group_hash){
	if (groups.count(group_hash) > 0){
		if (current_group == groups[group_hash]){
			return;
		}
		debug_suffix << "Apply group " << group_hash;
		debug(debug_suffix.str());

		current_group = groups[group_hash];
		majority = current_group.size()/2;
	}
}

// MISLEADING FUNCTION NAME!!!!!!!!
// This doesn't retrieve current_group.
// Rather, it finds the MOST RECENTLY COMMITTED Append Entry Config, then returns that group hash
string B4MeshOracle::GetCurrentGroup(){ 
	for (int i=commit_index; i>=0; --i){
		if (groups.count(log[i].second) > 0) { // If the log entry is an Append Entry Config
			return log[i].second; // Get the group of that entry
		}
	}
	return "";
}

string B4MeshOracle::SerializeGroup(string group_hash, vector<pair<int, Ipv4Address>> group){
	string ret = group_hash;

	for (auto n : group){
		int nodeId = n.first;
		ret += string((char*) &nodeId, sizeof(int));
	}

	return ret;
}

pair<string, vector<pair<int, Ipv4Address>>> B4MeshOracle::DeserializeGroup(string serie){
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

// Make a vector of [group hash -> group], ie Append Entry Configs.
// Either, a one-entry vector (if it is a simple split or merge),
// Or a compound change, which will have 2 config change entries, the first being a split.
vector<pair<string, vector<pair<int, Ipv4Address>>>> B4MeshOracle::
		MakeGroupChangeSequence(pair<string, vector<pair<int, Ipv4Address>>> new_group){

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
		// This is a compound config change. A split have to be commit first
		string hash = "";
		for (auto n : common_nodes)
		hash = hash + to_string(n.first);
		hash = hash + string(32 - hash.size(), '-');
		ret.push_back(make_pair(hash,common_nodes));
		ret.push_back(new_group);
	}
	return ret;
}

// Compare the given (new) group to the current_group, then identify if it's a SPLIT, MERGE or COMPOUND(?)
int B4MeshOracle::DetectGroupChange(vector<pair<int, Ipv4Address>> group){

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


bool B4MeshOracle::IsInCurrentGroup(Ipv4Address ip){

	for (auto g : current_group)
		if (g.second == ip) return true;
	return false;
}

int B4MeshOracle::LastCommitConfIndex(){
	for (int i=commit_index; i>=0; --i){
		if (groups.count(log[i].second) > 0) return i;
	}

	return -1;
}


int B4MeshOracle::GetNodeId(Ipv4Address ip){
	for (uint32_t i=0; i<peers.size(); ++i)
		if (peers[i] == ip) return i;
	return 0;
}

int B4MeshOracle::LastLogIndex(){
	if (log.size() == 0){
		return -1; //was -1?
	} else
		return log.size()-1;
}


Ptr<B4MeshOracle> B4MeshOracle::GetB4MeshOracleOf(int nodeId){
	Ptr<Application> app = ns3::NodeList::GetNode(nodeId)->GetApplication(0);
	Ptr<B4MeshOracle> consensus = app->GetObject<B4MeshOracle>();
	return consensus;
}

int B4MeshOracle::RemoveUncommittedEntries(){
	int ret = 0;
	while (commit_index < LastLogIndex()){
		debug_suffix << "Removing last uncommitted entry " << LastLogIndex();
		debug(debug_suffix.str());
		log.pop_back();
		ret++;
	}
	return ret;
}


void B4MeshOracle::SetMyB4Mesh(void * b4) {
	nodesB4mesh.push_back(b4);
}

void * B4MeshOracle::GetMyB4Mesh(int nid) {
	return nodesB4mesh.at(nid);
}

void B4MeshOracle::SetSendBlock(void func(void *, Block)){
	sendBlock = func;
}

void B4MeshOracle::SetIndicateNewBlock(void func(void *, Block)) {
	indicateNewBlock = func;
}

void B4MeshOracle::SetIndicateNewLeader(void func(void *)) {
	indicateNewLeader = func;
}


// B4Mesh (blockgraph) module calls this through SendBlockToConsensus()
void B4MeshOracle::InsertEntry(Block& b, int term){
	// Simulator::Schedule(MilliSeconds(sendServiceTime),
	// 			&B4MeshOracle::SendBlock, this, b);
	SendBlock(b, term);
}

// Leader adds block to its log and module. Is this even used??
void B4MeshOracle::SendBlock(Block& b){ // Should this be Block& or just Block?
	AppendLog(b, 1); // Term = 1 means just add to the current term
}

void B4MeshOracle::SendBlock(Block& b, int term){ // Should this be Block& or just Block?

	// For testing purposes
	int i = 1; // 1 if consensus, 0 if stub

	
	if (i==1) {
		AppendLog(b, term); // Consensus version
	} else {
		// Stub version: No consensus
		
		for (auto n : current_group) {
			GetB4MeshOracleOf(n.first)->IndicateBlockCommit(b);
		}
	}	
}

void B4MeshOracle::IndicateBlockCommit(Block b){

	// Note: this refers to the sendBlock VARIABLE and not the local SendBlock() function.
	// Which in turn points to the recvBlock in B4Mesh.
	// Should probably rename this.
	sendBlock(nodesB4mesh.at(node->GetId()), b); // Calling recvBlock in B4Mesh
	debug_suffix << "BLOCK HAS SUCCESSFULLY BEEN COMMITTED AND SENT BACK TO BLOCKGRAPH MODULE";
	debug(debug_suffix.str());
}

void B4MeshOracle::PrintLog(){
	for (auto l : log){
		if (groups.count(l.second) > 0)
		debug_suffix << l.second << ", ";
		else
		debug_suffix << dump(l.second.data(), 10) << ", ";
	}
	debug_suffix << "]";
	debug(debug_suffix.str());
}

void B4MeshOracle::debug(string suffix){
	cout << Simulator::Now().GetSeconds() << "s: B4MeshOracle : Node " << node->GetId() <<
      " : " << suffix << endl;
  	debug_suffix.str("");
}




