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
     * parameter. 
     */
    void ReceivePacket(Ptr<Socket> socket);

    /**
     * Send packet to the node's ip over an UDP socket.
     */
    void SendPacket(ApplicationPacket& packet, Ipv4Address ip, bool scheduled = false);

    /**
     * Sends a packet to all nodes.
     */
    void BroadcastPacket(ApplicationPacket& packet, vector<Ipv4Address> v);

    /* Extracts the type of message serialized in the payload, 
     * eg. CHILDLESSBLOCK_REQ 
     */
    int ExtractMessageType(const string& msg_payload);

  public:
    /**
     * Setup the application.
     */
    void SetUp(Ptr<Node> node, vector<Ipv4Address> peers, float timeBetweenTxn);

    /**
     * Method called at time specified by Start.
     */
    virtual void StartApplication();

    /**
     * Method called at time specified by Stop.
     */
    virtual void StopApplication();

    /**
     * This is a pointer to the oracle application installed on the node i.
     */
    Ptr<B4MeshOracle> GetB4MeshOracle(int nodeId);

    /**
     * Access to an other node's B4Mesh application via its Node ID.
     */
    Ptr<B4Mesh> GetB4MeshOf(int nodeId);

    /**
     * Method to get the Ipv4Address of the node.
     */
    Ipv4Address GetIpAddress();

    /**
     * Gets the Id of a node from its Ip Address.
     */
    int GetIdFromIp(Ipv4Address ip);

    /**
     * Gets the Ip address of a node from its ID.
     */
    Ipv4Address GetIpAddressFromId(int id);

  public:
    // Transaction methods

    /**
     * Check if transaction is in mempool, not in blockgraph, and mempool has space.
     * If so, add it to the mempool.
     */
    void TransactionsTreatment(Transaction t);

    /**
     * Checks if a transaction is already present in the mempool.
     */
    bool IsTxInMempool (Transaction t);
  
    /**
     * Checks that the mempool of the node is not full.
     */
    bool IsSpaceInMempool();

    /**
     * Updates the mempool once the Block is added to the BG.
     * Needs to remove transactions in a Block from the mempool.
     */
    void UpdatingMempool(vector<Transaction> txs, string b_hash);

    /**
     * A recursive method to make the node generate transactions.
     * Create transaction of variable size, then register the transaction.
     * After that, schedule a new call to itself after an interval.
     */
    void GenerateTransactions();

    /**
     * Create the actual transaction from payload, treat the transaction, then send
     * transaction to all nodes in the group after a delay. 
     */
    void RegisterTransaction(string payload);

    /**
     * For all transactions in mempool, broadcast all to all nodes in group again.
     */
    void RetransmitTransactions();

    /**
     * At regular intervals, ask for missing blocks (if any) 
     * and resubmit old transactions.
     */
    void RecurrentTasks();

    
    /**
     * Send the generated transaction to all node in same group.
     */
    void BroadcastTransaction(Transaction t);

  public:
    // Block methods

    /**
     * Do the treatment of a block before adding it to the blockgraph.
     */
    void BlockTreatment(Block block);

    /**
     * Checks if the hash of the block received as paremeter is present
     * in the list of blocks awaiting ancestors
     */
    bool IsBlockInWaitingList(string b_hash);
    
    /**
     * Checks if the hash of the block received as paremeter is present
     * in the list of missing childless blocks
     */
    bool IsBlockInMissingList(string b_hash);
    
    /**
     * Check if the parents of the received block are known by the node
     */
    bool AreParentsKnown(Block &block);

    /**
     * Takes a list of block hashes, and filters, returning only those
     * associated to blocks that are NOT in blockgraph yet.
     */
    vector<string> FilterNodesNotInBG(vector<string> parents); //Used to be GetParentsNotInBG

    /**
     * Takes a list of block hashes, and filters, returning only those 
     * NOT in list of blocks waiting for ancestors.
     * 6 May 2025: I think this is wrong. should be checking if parents are in missing list.
     * Going to redo the logic and test.
     */
    vector<string> FilterNodesNotInML(vector<string> p_notIn_BG);

    // This function does not seem to be used anywhere?
    // /**
    //  * Get the hashes of unknown parents of the received block
    //  */
    // vector<string> GetUnknownParents(vector<string> parents);
    
    // This function does not seem to be used anywhere?  
    // /**
    //  *  Start the fast synchronization process of any node
    //  *  after receiving a merge block
    //  */
    // void SyncNode(vector<Transaction> transactions);

    /**
     * After merge block is treated, start the sync process.
     * Treat the childless block hashes, send branch requests.
     */
    void StartSyncProcedure(vector<Transaction> transactions);

    /**
     * Sends request for missing branch to node that has it.
     */
    void SendBranchRequest();

    /**
     * During block treatment, if received block is in missing list, calls
     * this function to remove it from missing list.
     */
    void EraseMissingBlock(string b_hash);

    /**
     * During block treatment, if received block has missing parents that are also
     * not reflected in the missing list, update the missing list.
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
     * Only executed by the leader.
     * Checks the mempool state every certain time. If Txs in Mempool are
     * sufficient, and has been long enough since last block, then invoke GenerateBlocks();
     */
    bool TestPendingTxs();

    /*
     * If node is leader, TestPendingTxs() is true and allowed to create blocks,
     * make call to generate block. Repeat at adjustable interval
     */
    void TestBlockCreation();

    // Can merge the above 2 functions?

    
    /**
     * Create a template for a block, which is then passed and returned from merge and regular block.
     * creating functions to create the respective blocks. 
     * Then after a delay to simulate processing time (based on size of block). send block to consensus module.
     */ 
    void GenerateBlocks();

    /*
     * Return True if there are missing childless hashes.
     */
    bool IsMergeInProcess();

    /*
     * Take a blank slate block and turn it into a merge block. 
     */
    Block GenerateMergeBlock(Block &block);


    /*
     * Take a blank slate block and turn it into a regular block. 
     */
    Block GenerateRegularBlock(Block &block);

    /*
     * Return list of transactions based on time stamp
     */
    vector<Transaction> SelectTransactions();

  public:
    // REQUEST BLOCK methods

    /**
     * Checks if there are still missing blocks in the missing_block_list. 
     * If so, it will send request for them to the node who
     * sent the child block of the missing parent [??].
     */
    void Ask4MissingBlocks();

    /**
     * Send block with that hash to the ip address, in response to an Ask4MissingBlocks()
     */
    void SendBlockTo(string hash, Ipv4Address ip);

  public:
    //  Change topology methods

    /**
     * Only called by the leader. Starts the merge process.
     */
    void StartMerge();

    /**
     * Return list of nodes that were not in this partition previously. 
     */
    vector<pair<int, Ipv4Address>> GetNewNodes ();

    /**
     * Return list of nodes that were not in this partition previously. 
     */
    vector<pair<int, Ipv4Address>> GetNodesFromOldGroup ();

    /**
     * Leader request for childless blocks. 
     */
    void Ask4ChildlessHashes();

    /**
     * Ask4ChildlessHashes() sets a timer for 6 seconds, once that's done it assumes
     * the requests have all been responded to. Then schedules call to this function
     * to allow node to create blocks and create a merge block, then calls the GenerateBlocks()
     * function.
     */
    void timer_childless_fct(void);

    /**
     * Non-leader sends the childless block hashes to leader upon request.
     */
    void SendChildlessHashes(Ipv4Address ip);

    /**
     * Called by leader, on message sent by the SendChildlessHashes() function.
     * For each of the childless hashes sent, treat the childless hash.
     * Then check if can create a merge block.
     */
    void ProcessChildlessResponse(const string& msg_payload, Ipv4Address ip);

    /**
     * TODO: complete this
     */
    void ChildlessHashTreatment(string childless_hash, Ipv4Address ip); // Was ChildlessBlockTreatment

    /**
     * Set conditions to allow merge block creation and make call to generate it early (before time expires)
     * IFF all the required branches have been collected.
     */
    void CheckMergeBlockCreation();

    /**
     * Update the recover_branch with the childless hash mapped to the ip of the sender.
     */
    void RegisterNode(string childless, Ipv4Address ip);

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

    double timeBetweenTxn;   // Mean time between transactions
    double startmergetime;
    double endmergetime;
    double count_pendingTx;  // Counts the number of pending transactions
    double count_missingBlock;  // Counts the number of successfully recovered missing blocks
    double count_waitingBlock;  // Counts the number of successfully added blocks from the waiting list
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

    bool running; // True if application is running.
    bool mergeBlock; // True if node is currently creating a merge block.
    bool createBlock; // True if node is allowed to create a block now (merge or regular).
    bool startMerge; // True if merge process has started and not ended. TRUE IF NODE IS IN STABLE TOPOLOGY, allows
    //                  StartMerge() to proceed. 

    int lastChange;  // Last change nature. MERGE or SPLIT
    int lastBlock_creation_time;  // Time of last block creation.
    int change;
    int lastLeader; // Leader of the latest block to be treated.

    // Ip address of all nodes of the system.
    // At index i, node with ip = peers[i] has node id i.
    vector<Ipv4Address> peers;  

    // GroupId of a group
    string groupId;   

    // Current group that node is in. Each element is a pair containing Node ID and IP address.
    vector<pair<int, Ipv4Address>> group;   

    // Previous group that node was in. Each element is a pair containing Node ID and IP address.
    vector<pair<int, Ipv4Address>> previous_group; 

    // List of id of groups that a node has been a part of.
    vector<string> groupId_register; 

    // Mempool of transactions. Maps transaction hash to the Transaction.
    map<string, Transaction> txn_mempool;

    // The hashes of the childless block of the branches to recover.
    vector<string> missing_childless_hashes; // Was: missing_childless

    // Mapping of childless hashes, and sender address (IP address of block to retrieve it from)
    // Also includes parent blocks known to be missing, as their child is present in the waiting list.
    // If hash is for missing parent, id will be that of the leader of the child block that is missing its parents.
    // If hash is for missing childless, id will be that of the node that send this childless hash(?) 
    multimap<string, Ipv4Address> missing_block_list; 

    // Blocks that cannot be added to Blockgraph yet as they are waiting for ancestors.
    // Maps Block hash to the block.
    map<string, Block> block_waiting_list; 

    // Node ID of node that has the branch of the childless block to send.
    // Maps childless block hash to node id of node to request branch from.
    multimap<string, int> recover_branch;
    
    Blockgraph blockgraph;  // The database of blocks
    
    stringstream debug_suffix;
};
#endif
