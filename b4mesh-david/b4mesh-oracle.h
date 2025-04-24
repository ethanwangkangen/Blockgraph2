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
    enum{NONE, SPLIT, MERGE, ARBITRARY};

  public:
    static TypeId GetTypeId(void);

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
    Ipv4Address GetIpAddress(int i);

    /**
     * Getters and Setters
     */

    int GetCurrentLeader();

    void SetLeaderToNull();

    int GetCurrentState();


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
     * Set the current state of the node.
     */
    void SetCurrentState(int st);

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
     * Sends to the other nodes a new block
     */
    void SendBlock(Block b);

    /**
     * Receives a block and schedules its commitment
     */
    void RecvBlock(Block b);

    /**
     * Inddicates the other nodes a block is commited
     */
    void IndicateBlockCommit(Block b);

    /**
     * Include the reference for the function that should be called in B4Mesh app to
     * indicate the election of a new leader
     */
    void SetIndicateNewLeader(void func(void *));

    /**
     * Schedule a new election
     */
    void StartElection();

    /**
     * Defines the winner, namely, the new leader
     */
    void EndElection(uint32_t t_elect);

    /**
     * Set the probability of Receiving a block from the leader
     */
    void SetBlockRecvProbability(double b_rcv);

    /**
     * Set the probability of Receiving the commit for a block from the leader
     */
    void SetblockCommitProbability(double b_com);

    /**
     * Calculate and set the majority of the current group
     */
    void SetMajority();

    /**
     * Get the majority of the current group
     */
    float GetMajority();

    /**
     * Verifies whether the node is the current leader or not
     */
    bool IsLeader();

    // Pedro: tests
    void test(void func(int, void*), void * b4);

      /**
     * Insert a block to the Oracle consenus.
     */
    void InsertEntry(Block& b, int term = -1);

      /**
     * Change the current group and trigger a new election when needed.
     */
    void ChangeGroup(pair<string, vector<pair<int, Ipv4Address>>> new_group, int natchangete);



  private:

    /**
     * Verifies whether a specific node is in the current group or not
     */
    bool IsInCurrentGroup(Ipv4Address ip);

    /**
     * Set a random uniform variable to be the coin of the node
     */
    void SetCoin();


    /**
     * Return the id of the node whose ip is passed in the argument.
     */
    int GetNodeId(Ipv4Address ip);


  private:

    bool electionOn;
    uint32_t term;

    static vector<void *> nodesB4mesh;      // keeps the reference for all b4mesh nodes

    // Service Times
    static double sendServiceTime;       // milliseconds
    static double leaderElectionDelay;   // seconds
    static double blockCommitDelay;      // milliseconds


    // Probabilities
    double blockRecvProbability;
    double blockCommitProbability;

    Ptr<UniformRandomVariable> coin;

    // Group management
    float majority;


    vector<pair<int, Ipv4Address>> current_group;   // Nodes in the same group as the this one
    vector<Ipv4Address> peers;                      // Whole nodes of the application

    Ptr<Node> node;


    int current_state;        // State of the node (LEADER, FOLLOWER, CANDIDATE)
    int current_leader;       // id of the current leader


    // Variables related to NS3
    bool running;


    stringstream debug_suffix;


  public:

    // B4Mesh interfaces
    void (* indicateNewBlock)(void *, Block);   //  indicate new block is committed
    void (* sendBlock)(void *, Block);          //  send block
    void (* indicateNewLeader)(void *);         //  indicate new leader

    pair<float, int> received_bytes;
    pair<float, int> sent_bytes;
    pair<float, int> received_messages;
    pair<float, int> sent_messages;

    B4MTraces* traces;
};
#endif
