#ifndef B4MESH_H
#define B4MESH_H

#include "ns3/application.h"
#include "ns3/address.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/core-module.h"
#include "ns3/string.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-address.h"
#include "ns3/node-list.h"
#include "ns3/olsr-routing-protocol.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/random-variable-stream.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include "ns3/mobility-model.h"

#include "application_packet.h"
#include "transaction.h"
#include "block.h"
#include "blockgraph.h"
#include "b4m_traces.h"
#include "b4mesh-oracle.h"

#include <vector>
#include <utility>
#include <random>
#include <limits>
#include <queue>
#include <math.h>
#include <unordered_map>

#include "configs.h"

using namespace ns3;
using namespace std;

class B4Mesh : public Application{

  public:
    static const int TIME_UNIT = 10;
    enum{CHILDLESSBLOCK_REQ, CHILDLESSBLOCK_REP,
         GROUPBRANCH_REQ, GROUPCHANGE_REQ};
    enum{NONE, SPLIT, MERGE, ARBITRARY};

  public:
    typedef struct childlessblock_req_hdr_t {
      int msg_type;
    } childlessblock_req_hdr_t;

    typedef struct childlessblock_rep_hdr_t {
      int msg_type;
    } childlessblock_rep_hdr_t;

    typedef struct group_branch_hdr_t {
      int msg_type;
    } group_branch_hdr_t;

  public:
    static TypeId GetTypeId(void);

  public:
    /**
     * Constructors and destructor
     */
    B4Mesh();
    ~B4Mesh();

  private:
    /**
     * Callback function to process incoming packets on the socket passed in
     * parameter. Basically, this function implement de protocol of the
     * blockgraph application.
     */
    void ReceivePacket(Ptr<Socket> socket);
    /**
     * Send packet to the node's ip over an UDP socket.
     */
    void SendPacket(ApplicationPacket& packet, Ipv4Address ip, bool scheduled = false);
    /**
     * Sends a packet to all nodes'
     */
    void BroadcastPacket(ApplicationPacket& packet, vector<Ipv4Address> v);
    /* Retunes the type of message serialized in the payload */
    int ExtractMessageType(const string& msg_payload);

  public:
    /**
     * Setup the application
     */
    void SetUp(Ptr<Node> node, vector<Ipv4Address> peers, float txMean);
    /**
     * Method called at time specified by Start.
     */
    virtual void StartApplication();
    /**
     * Method called at time specified by Stop.
     */
    virtual void StopApplication();
    /**
     * This is a pointer to the oracle application installed on the
     * node i
     */
    Ptr<B4MeshOracle> GetB4MeshOracle(int nodeId);
    /**
     * Access to an other node B4Mesh application via its Node ID
     */
    Ptr<B4Mesh> GetB4MeshOf(int nodeId);
    /**
     * Method to get the Ipv4Address of the node.
     */
    Ipv4Address GetIpAddress();
    /**
     * Gets the Id of a node from its Ip Address
     */
    int GetIdFromIp(Ipv4Address ip);
    /**
     * Gets the Ip address of a node from its ID
     */
    Ipv4Address GetIpAddressFromId(int id);

  public:
    // Transaction methods
    /**
     * Do the treatment of a transaction when received
     */
    void TransactionsTreatment(Transaction t);
    /**
     * Checks if a transaction is already present in the mempool
     */
    bool IsTxInMempool (Transaction t);
  
    /**
     * Checks that the mempool of the node is not full
     */
    bool IsSpaceInMempool();
    /**
     * Updates the Txs Mempool once the Block is added to the BG
     */
    void UpdatingMempool(vector<Transaction> txs, string b_hash);
    /**
     * A recursive method to make the node generate transactions
     */
    void GenerateTransactions();

    void RegisterTransaction(string payload);

    void RetransmitTransactions();

    void RecurrentTasks();

    void timer_childless_fct(void);

    /**
     * Send the generated transaction to all node in same group
     */
    void SendTransaction(Transaction t);

  public:
    // Block methods
    /**
     * Do the tratment of a block befor adding it to the blockgraph
     */
    void BlockTreatment(Block block);

    bool IsBlockInWaitingList( string b_hash);
    /**
     * Checks if the hash of the block received as paremeter is present
     * in the missing_block_list
     */
    bool IsBlockInMissingList(string b_hash);
    /**
     * Check if the parents of the received block are known by the node
     */
    bool AreParentsKnown(Block &block);

// NEW FUNCTIONS **************
    vector<string> GetParentsNotInBG(vector<string> parents);
    vector<string> GetParentsNotInWL(vector<string> p_notIn_BG);

    /**
     * Get the hashes of unknown parents of the received block
     */
    vector<string> GetUnknownParents(vector<string> parents);
  
    /**
     *  Start the fast synchronization process of any node
     *  after receiving a merge block
     */
    void SyncNode(vector<Transaction> transactions);
    void StartSyncProcedure(vector<Transaction> transactions);

    void SendBranchRequest();
    /**
     * Updates the missing_block_list. If new block is a missing parent
     * it will ERASE it form the list
     */
    void EraseMissingBlock(string b_hash);
    /**
     * Updates the missing_block_list. If new block has missing parents
     * it will ADD them to the missing_block_list
     */
    void UpdateMissingList(vector<string> unknown_p, Ipv4Address ip);
    /**
     *  Checks that the parents of the blocks in the waiting list are
     *  all present in the local BG. If so, it add the block to the local
     *  BG and erase it from the waiting list
     */
    void UpdateWaitingList();
    /**
     *  Add the block to the local blockgraph
     */
    void AddBlockToBlockgraph(Block block);
    /*
     * This method checks the Mempool state every certain time. If Txs in Mempool are
     * sufficients, then invok GenerateBlocks();
     */
    bool TestPendingTxs();

    void TestBlockCreation();

    /**
     * A method that generates a new block when Txs in Mempool are enough to
     * create a block. -Only Leader -Broadcast block to the local group
     */
    void GenerateBlocks();

    bool IsMergeInProcess();

    Block GenerateMergeBlock(Block &block);

    Block GenerateRegularBlock(Block &block);

    vector<Transaction> SelectTransactions();

  public:
    // REQUEST BLOCK methods
    /**
     * This method checks every certain time if there still missing blocks in the
     * missing_block_list. If so, it will ask for them to the node who
     * send the child block of the missing parent.
     */
    void Ask4MissingBlocks();
    /**
     * It's the answer of Ask4ParentBlock(). It sends the missing parent of a block
     * to the receiver.
     */
    void SendBlockto(string hash, Ipv4Address ip);

  public:
    //  Change topology methods
    /**
     * Starts the merge process of the leader node.
     */
    void StartMerge();

    //void GetNewNodes();
    vector<pair<int, Ipv4Address>> GetNewNodes ();

    vector<pair<int, Ipv4Address>> GetNodesFromOldGroup ();
    /**
     * Method executed by the leader when a topology change is detected
     * This method causes a CHILDLESSBLOCK_REQ
     */
    void Ask4ChildlessBlocks();
    //void Ask4ChildlessBlocks(vector<pair<int, Ipv4Address>> new_nodes);
    /**
     * Send the childless blocks to leader upon a CHILDLESSBLOCK_REQ
     */
    void SendChildlessBlocks(Ipv4Address ip);
    /**
     * The leader checks 4 missing side-chains from followers CHILDLESSBLOCK_REP
     * If a childless block groupId is unkwnow, leader ask for the whole branch
     */
    void ProcessChildlessResponse(const string& msg_payload, Ipv4Address ip);

    void RegisterNode(string childless, Ipv4Address ip);

    void ChildlessBlockTreatment(string childless, Ipv4Address ip);

    void CheckMergeBlockCreation();

    /**
     * Send a full branch to a node
     */
    void SendBranch4Sync(const string& msg_payload, Ipv4Address ip);

  public:
    // Interfaces with Consensus and Group Management (Topology changes)
    /**
     * Get the new group and the nature of a change of topology
     */
    void ReceiveNewTopologyInfo(pair<string, vector<pair<int, Ipv4Address>>> new_group, int natchange);

    void UpdateTopologyInfo(uint32_t change, pair<string, vector<pair<int, Ipv4Address>>> new_group);
    /**
     *  Send the created block to the consensus layer
     */
    void SendBlockToConsensus(Block b);
    /**
     *  The consensus layer notifies the blockgraph protocol of a new commited
     *  block.
     */
    static void RecvBlock(void * b4, Block b);
    /**
     * This function send the block received from the consensus to the
     * BlockTreatment function.
     */
    void GetBlock(Block b);
    /**
     *  This method is an interface btw consensus and blockgraph.
     *  It notifies the blockgraph protocol of the adoption of a new leader
     */
    static void HaveNewLeader(void * b4);
    /*
     *  This function gets the last nature change in the network topology
     *  ex. Lastchange = MERGE
     */
    int GetLastChange();


  private:
    // Performances and traces methods
    void RecurrentSampling ();

    int GetNumTxsGen();
    int GetNumReTxs();

    /**
     * Calcul the size of the mempool in bytes
     * (Only needed for getting performances of the system)
     */
    int SizeMempoolBytes (); // Check ok! 

     /**
     *  This function keeps track of tranmsactions in mempool
     */
    void MempoolSampling (); // Check ok! 

    void TxsPerformances ();

     /**
     * This function save the traces to calculate the block creation rate.
     */
    void BlockCreationRate(string hashBlock, string b_groupId, int txs, double blockTime);   //Check ok!

    /**
     * Check if the transactions in Block b, are already in the BG
     * (Only use to get performances of the system)
     */
    void Check4RepTxs (vector<Transaction> txs, string hash);

    /**
     * block performances for delay calculation
     */
    void TraceBlockSend(int nodeId, double timestamp, int hashBlock);
    /**
     * block performances for delay calculation
     */
    void TraceBlockRcv(double timestamp, int hashBlock);
    /**
     * transaction performances for delay calculation
     */
    void TraceTxsSend(int nodeId, int hashTx, double timestamp);
    /**
     * transaction performances for delay calculation
     */
    void TraceTxsRcv(int hashTx, double timestamp);

    void TraceTxLatency(Transaction tx, string b_hash);
   
    /**
     * Function that generate the blockgraph file
     */
    void CreateGraph(Block &block);
    /**
     * This function generates files for performances
     */
    void GenerateResults();
    /**
     * This function generates files for performances
     */
    void PerformancesB4Mesh();
    /*
     *  This function enable the printing of logs in the output
     *  by commenting this functin logs are not printed.
     */
    void debug(string suffix);
    /**
     * The execution time is modeled by a Lognormal random variable
     * Lognormal -- (mean, sd)
     * The mean and sd is related to the size of the block
     */
    float GetExecTime(int size);

    float GetExecTimeBKtreatment(int NBblock);


    float GetExecTimeTXtreatment(int NBblock);

    void CourseChange(string context, Ptr<const MobilityModel> mobility);

  public:
    /* Traces for B4Mesh App */
    float p_b_t_t; // Processing block treatment time
    float p_b_c_t; // Processing block creation time
    float p_t_t_t; // Processing transaction treatment time
    float p_t_c_t; // Processing transaction creation time
    map <int,pair<double,double>> time_SendRcv_txs; // TxHash, TimeSend, TimeRcv
    vector <pair<int, pair <int, int>>> blockgraph_file; //
    map <double, int> b4mesh_throughput;
    map <string, pair<string,string>> repeat_txs;
    B4MTraces* traces;

  private:
    // for performances
    int lostTrans; // counts the number of transactions lost because of space limit
    int lostPacket; // counts the number of packets lost because of network jamming
    int numTxsG; // The number of transaction generated by the local node
    int numRTxsG; // The number of transactions retransmitted 
    int numDumpingBlock; // The number of dumped blocks because either is alredy in the blockgraph or in the waiting list
    int numDumpingTxs; // The number of dumped txs because rither is already in blockgrap or in mempool

    double sentPacketSizeTotal; // Total size of all packets sent
    double sentTxnPacketSize; // Total size of all transaction packets sent

    double interArrival;   // the mean time of the generation of a transaction
    double startmergetime;
    double endmergetime;
    double count_pendingTx;  // Counts the number of pending transactions
    double count_missingBlock;  // Counts the number of successfuly recover missing blocks
    double count_waitingBlock;  // Counts the number of successfuly added blocks from the waiting list
    double total_pendingTxTime;  // The cumulative time that transactions spends in the mempool
    double total_missingBlockTime;  // The cumulative time that blocks' references spends in the missing_block_list
    double total_waitingBlockTime;  // The cumulative time that blocks spends in the waiting list
    map<string, double> pending_transactions_time;  // Time when a transaction enter the mempool
    map<string, double> waiting_list_time;  // Time when a block enter the waiting List
    map<string, double> missing_list_time;  // Time when a block's reference is insert in the missing block List
    map <int,pair<double,double>> time_SendRcv_block; //BlockHash, TimeSend, TimeRcv
    //multimap<string, pair<string, double >> TxsLatency;
    unordered_multimap<string, pair<string, double >> TxsLatency;
    // map<double, pair<float, int>> txs_perf;
    vector<pair<pair<double, float>, pair<int, int>>> txs_perf;
    // map <double, pair<int, int>> mempool_info; // for traces propuses
    vector<pair<pair<double, int>, pair<int, float>>> mempool_info;

  private:
    // private member variables of blockgraph protocol 
    Ptr<Socket> recv_sock;
    Ptr<Node> node;
    bool running;
    bool mergeBlock;
    bool createBlock;
    bool startMerge;
    int lastChange;  // Last change nature
    int lastBlock_creation_time;  // Time of last block creation.
    int change;
    int lastLeader; // Work in progress
    string groupId;   // GroupId of a group
    vector<string> groupId_register;
    vector<Ipv4Address> peers;  // Ip address of all nodes of the system
    vector<pair<int, Ipv4Address>> group;   // Nodes belonging to the same group
    vector<pair<int, Ipv4Address>> previous_group;
    vector<string> missing_childless; //the hashes of the childless block of the branches to recover
    map<string, Transaction> pending_transactions;  // mempool of transactions
    map<string, Block> block_waiting_list; // blocks waiting for their ancestors
    multimap<string, Ipv4Address> missing_block_list;
    //multimap<int, string> recover_branch; // Nodo ID - Hash of the childless block
    multimap<string, int> recover_branch;
    Blockgraph blockgraph;  // The database of blocks
    stringstream debug_suffix;
};
#endif
