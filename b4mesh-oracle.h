	#ifndef B4M_ORACLE_H
	#define B4M_ORACLE_H

	#include "ns3/application.h"
	#include "ns3/address.h"
	#include "ns3/socket.h"
	#include "ns3/udp-socket.h"
	#include "ns3/socket-factory.h"
	#include "ns3/packet.h"
	#include "ns3/core-module.h"
	#include "ns3/string.h"
	#include "ns3/ipv4.h"
	#include "ns3/node-list.h"
	#include "ns3/traced-value.h"
	#include "ns3/trace-source-accessor.h"
	#include "ns3/random-variable-stream.h"

	#include "ns3/log.h"
	#include "ns3/simulator.h"

	#include "application_packet.h"
	#include "transaction.h"
	#include "block.h"
	#include "b4m_traces.h"

	#include <vector>
	#include <utility>
	#include <random>
	#include <cassert>
	#include <deque>
	#include <sstream>

	using namespace ns3;
	using namespace std;

class B4MeshOracle : public Application{

	public:
		static const int TIME_UNIT = 10;
		enum{LEADER, FOLLOWER, CANDIDATE};
		enum{REQ_VOTE, ACK_VOTE, APPEND_ENTRY, ACK_ENTRY, APPEND_ENTRY_CONF};
		enum{NONE, SPLIT, MERGE, ARBITRARY};

	public:
		static TypeId GetTypeId(void);

		typedef struct request_vote_hdr_t {
			int msg_type;             // type of the message
			int term;                 // candidate's term
			int candidate_id;         // candidate requesting vote
			int last_log_index;       // index of candidate's last vote entry
			int last_log_term;        // term of candidate's last log entry
		} raft_request_vote_hdr_t;

		typedef struct append_entries_hdr_t {
			int msg_type;       // type of the message
			int term;           // leader's term
			int leader_id;      // so follower can redirect clients
			int prev_log_index; // Index of the log entry immediately
			int prev_log_term;  // Term of the prevLogIndex entry
			int leader_commit;  // Leader's commit Index
			int entry_term;     // Term of the entry stored in that message. -1 if it
								// is an heartbeat.
		} raft_append_entries_hdr_t;

		typedef struct request_vote_ack_t{
			int msg_type;       // type of the message
			int term;           // current term for candidate to update itself
			int commit_index;   // index of the last committed entry of the sender
			char vote_granted;  // true means candiate received vote
		} request_vote_ack_t;

		typedef struct append_entries_ack_t{
			int msg_type;       // type of the message
			int term;           // current term for candidate to update itself
			int commit_index;   // index of the last committed entry of the sender
			int entry_index;    // index of the entry that is acknowledged
			int id;             // id of the node sending the ack
			char success;       // true if follower contained entry matching
								// prevLogIndex and prevLogTerm
		} append_entries_ack_t;


	public:
		/**
		* Constructors and destructor
		*/
		B4MeshOracle();
		~B4MeshOracle();

		/**
		* Setup the application
		*/
		void SetUp(Ptr<Node> node, vector<Ipv4Address> peers);

		/**
		* Method called at time specified by Start.
		*/
		virtual void StartApplication();

		/**
		* Method called at time specified by Stop.
		*/
		virtual void StopApplication();


		/**
		* Get a reference for the Oracle application of a specific node.
		*/
		Ptr<B4MeshOracle> GetB4MeshOracleOf(int nodeId);

		/**
		* Method to get the Ipv4Address of the node.
		*/
		Ipv4Address GetIpAddress();

		/**
		* Method to get the Ipv4Address of a node
		* from it's node Id.
		*/
		Ipv4Address GetIpAddress(int i);

		/**
		* Getters and Setters
		*/
		vector<int> GetNextIndexes();

		vector<int> GetMatchIndexes();

		int GetCommitIndex();

		int GetLastApplied();

		int GetLogTerm(int index);

		vector<pair<int,string>>& GetLog();

		int GetCurrentLeader();

		void SetCurrentLeader(int ld);

		int GetVotedFor();

		int GetCurrentTerm();

		int GetCurrentState();
		
		void SetCurrentState(int i);

		int GetLastLogTerm();

		float GetMajority();

		void SetMajority();

		pair<int, string> GetEntry(int index);


	public:

		/**
		* Include my reference of the B4Mesh application into the static vector nodesB4mesh.
		*/
		void SetMyB4Mesh(void * b4);

		/**
		* Get the reference for the B4Mesh application of a specific node.
		*/
		void * GetMyB4Mesh(int nid);

		/**
		* Include the reference for the function that should be called in B4Mesh app to
		* commit a block
		*/
		void SetIndicateNewBlock(void func(void *, Block));

		/**
		* Include the reference for the function that should be called in B4Mesh app to
		* send a block
		*/
		void SetSendBlock(void func(void *, Block));

		/**
		* Include the reference for the function that should be called in B4Mesh app to
		* indicate the election of a new leader
		*/
		void SetIndicateNewLeader(void func(void *));

		/**
		* Verifies whether the node is the current leader or not
		*/
		bool IsLeader();

		/**
		* Insert a block to the Oracle consenus.
		*/
		void InsertEntry(Block& b, int term = -1);

		/**
		* Change the current group and trigger a new election when needed.
		*/
		//void ChangeGroup(pair<string, vector<pair<int, Ipv4Address>>> new_group, int natchangete);
		void ChangeGroup(pair<string, vector<pair<int, Ipv4Address>>> new_group);

		/**
		* Apply the last pending group as the current group
		*/
		void ApplyGroup(string group_hash);

		/**
		* Get the last group hash committed in the log.
		*/
		string GetCurrentGroup();

		/**
		* Make the sequence of group changes in case of composed group changes.
		*/
		vector<pair<string, vector<pair<int, Ipv4Address>>>> MakeGroupChangeSequence(pair<string, vector<pair<int, Ipv4Address>>> new_group);

		/**
		* Return the index of the last log entry.
		*/
		int LastLogIndex();


		/**
		* Sends to the other nodes a new block
		*/
		void SendBlock(Block& b); // Should this be Block& or just Block?
		void SendBlock(Block& b, int term);

		/**
		* Inddicates the other nodes a block is commited
		*/
		void IndicateBlockCommit(Block b);

		// // Pedro: tests
		// void test(void func(int, void*), void * b4);

		void AppendLog(Block&b, int term);


	private:

		// Methods implementing the raft protocol
		void ResetElectionTimeout(float factor = 1); // Reset the election timeout of the node
		void TriggerElectionTimeout(); // Function that is trigger on election
									// timeout.
		int ExtractMessageType(const string& msg_payload); // Return the type of the
														// message serialized in payload
		/**
		* Method to process incoming vote requests.
		*/
		request_vote_ack_t ProcessRequestVote(request_vote_hdr_t msg);

		/**
		* Method to process incoming vote requests responses.
		*/
		void ProcessVoteResponse(request_vote_ack_t msg);

		/**
		* Method to periodically send heartbeats.
		*/
		void SendHeartbeats();

		/**
		* Method to process AppendEntries message.
		*/
		append_entries_ack_t ProcessAppendEntries(string data_payload);

		/**
		* Method ot process AppendEntries acknowledgment message.
		*/

		void ProcessEntriesAck(append_entries_ack_t ack_entries);

		/**
		* Return the type of group change related to the current configuration
		*/
		int DetectGroupChange(vector<pair<int, Ipv4Address>> group);

		/**
		* Return true if a node's ip is in the current group
		*/
		bool IsInCurrentGroup(Ipv4Address ip);

		/*
		* Return the index of the last commit config entry.
		*/
		int LastCommitConfIndex();

		/**
		* Remove uncommitted entries from the log. Return the number of removed
		* entries.
		*/
		int RemoveUncommittedEntries();

		/**
		* Print the log's data on the standard output.
		*/
		void PrintLog();

		/**
		* Print a debug message with a greppable prefix for this module.
		*/
		void debug(string suffix);

	private:
		/**
		* Callback function to process incoming packets on the socket passed in
		* parameter. Basically, this function implement de protocol of the
		* raft consensus.
		*/
		void ReceivePacket(Ptr<Socket> socket);

		/**
		* Send packet to the node's ip over an UDP socket.
		*/
		void SendPacket(ApplicationPacket& packet, Ipv4Address ip, bool scheduled = false);

		/**
		* Broadcast a packet to all peers known by the server
		*/
		void BroadcastPacket(ApplicationPacket& packet);

		/**
		* Serialize and deserialize the list of servers that represent a group.
		*/
		string SerializeGroup(string group_hash, vector<pair<int, Ipv4Address>> group);
		pair<string, vector<pair<int, Ipv4Address>>> DeserializeGroup(string serie);

		/**
		* Return the id of the node whose ip is passed in the argument.
		*/
		int GetNodeId(Ipv4Address ip);

	private:
		//uint32_t term;

		// // Service Times
		// static double sendServiceTime;       // milliseconds
		// static double leaderElectionDelay;   // seconds
		// static double blockCommitDelay;      // milliseconds


		// // Probabilities
		// double blockRecvProbability;
		// double blockCommitProbability;

		// Ptr<UniformRandomVariable> coin;

		// Group management
		
	
		static vector<void *> nodesB4mesh;      // keeps the reference for all b4mesh nodes


		vector<pair<int, Ipv4Address>> current_group;   // Nodes in the same group as the this one
		vector<Ipv4Address> peers;                      // Whole nodes of the application

		vector<pair<string, vector<pair<int, Ipv4Address>>>> pending_groups; // Pending group waiting to be
												// commited in the raft log to be applied
		
		map<string, vector<pair<int, Ipv4Address>>> groups; // List of all groups registered

		Ptr<Socket> recv_sock;
		Ptr<Node> node;

		// State of the server
		int current_state; // State of the server (LEADER, FOLLOWER, CANDIDATE)
		int current_term; // last term server has seen
		int voted_for; // candidateId that received vote in current term
		int current_leader; // id of the current leader
		vector<pair<int,string>> log; // Log entries, each entry contains the term
				// when entry was received and the hash of the data in the entry.
				// the position of the entry in the log is its index.

		int commit_index; // index of the highest log entry know to be commited
		int last_applied; // inedx of the highest log entry applied to state machine

		vector<int> next_indexes; // for each server, index of the next log entry to
								// send to that server
		vector<int> match_indexes;// for each server, index of the highest log entry
								// known to be replicated on server
		vector<int> commit_indexes; // for each server, index of the last committed
									// entry by the server
		int gathered_votes;


		// Variables related to NS3
		EventId election_timeout_event;
		bool running;

		map<string, Block> blockpool; // Data waiting to be broadcasted on the network
		float heartbeat_freq; // Time between each heartbeat
		pair<float, float> election_timeout_parameters; // Bound to select the waiting time for the election timeout.

		stringstream debug_suffix;


	public:

		// B4Mesh interfaces
		void (* indicateNewBlock)(void *, Block);   //  indicate new block is committed
		void (* sendBlock)(void *, Block);          //  send block
		void (* indicateNewLeader)(void *);         //  indicate new leader

		float start_election;
		float end_election;
		float majority;
		float noLeaderBegin;

		
		pair<float, int> received_bytes;
		pair<float, int> sent_bytes;
		pair<float, int> received_messages;
		pair<float, int> sent_messages;


		double sentPacketSizeTotalConsensus; // Total size of all packets sent

		B4MTraces* traces;
		
		
};

#endif
