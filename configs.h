#ifndef B4MESH_CONFIGS
#define B4MESH_CONFIGS

// Transaction Payload size
#define TX_PAYLOAD_MIN 300
#define TX_PAYLOAD_MAX 600
#define TX_MEAN_SIZE 450

// Minimum time to propagate a block before creating a new one
#define TIME_BTW_BLOCK 15

// Number of transactions allowed in a block
#define MAX_SIZE_BLOCK 54  // In KB

// Minimum number of transactinos in mempool to create a block
#define MIN_SIZE_BLOCK 36  // In KB

// Size of the mempool
#define SIZE_MEMPOOL 5000  // In KB
//Was originally 500. Increased by tenfold to test.

#define SEC_60_TIMER 60

// B4Mesh recurrent tasks
#define SEC_10_TIMER 10

// B4esh recurrent sampling
#define SEC_5_TIMER 5

// B4Mesh Timer to test block creation
#define TESTMEMPOOL_TIMER 1.5

// Transaction retransmission timer
#define RETRANSMISSION_TIMER 60

// b4mesh-mobility
#define TOPOLOGY_TOLERANCE_TIME 10 //was 3

// Block creation delay
// Block treatment delay
// Transaction creation delay
// Transaction treatment delay

// Traces output
#define TRACE std::cout << Simulator::Now().GetSeconds() << " " << "NODE" << " " << node->GetId() << " "

#endif  /* B4MESH_CONFIGS */
