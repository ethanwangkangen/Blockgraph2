#include "b4m_traces.h"

B4MTraces::B4MTraces(){
  start_election = -1;
  start_config_change = -1;
}

void B4MTraces::StartElection(float timestamp){
  if (start_election == -1){
    start_election = timestamp;
  }
}

void B4MTraces::EndElection(float timestamp){
  if (start_election != -1){
    election_delay[start_election] = timestamp - start_election;
//    cerr << "Delay d'election a " << start_election << " : " << election_delay[start_election] << endl;
    start_election = -1;
  }
}

void B4MTraces::ResetStartElection(){
  start_election = -1;
}

void B4MTraces::StartConfigChange(float timestamp){
  if (start_config_change == -1)
    start_config_change = timestamp;
}
void B4MTraces::EndConfigChange(float timestamp){
  if (start_config_change != -1){
    config_change_delay[start_config_change] = timestamp - start_config_change;
//    cerr << "Configuration change delay at " << start_election << "s : " << configuration_change_delay[start_election] << endl;
  }
}
void B4MTraces::ResetStartConfigChange(){
    start_config_change = -1;
}

void B4MTraces::ReceivedBlockInfo(pair<pair<int, int>, pair<int, double> > block_creation_rate){
  block_creation.push_back(block_creation_rate);
}

void B4MTraces::ReceivedBytes(pair<float, int> new_value){
//  cerr << "Received " << new_value.second << " bytes at " << new_value.first <<
//    "s" << endl;
  if (received_bytes.count(new_value.first) >= 1)
    received_bytes[new_value.first] += new_value.second;
  else
    received_bytes.insert(new_value);
}

void B4MTraces::SentBytes(pair<float, int> new_value){
//  cerr << "Send " << new_value.second << " bytes at " << new_value.first <<
//    "s" << endl;

  if (sent_bytes.count(new_value.first) >= 1)
    sent_bytes[new_value.first] += new_value.second;
  else
    sent_bytes.insert(new_value);
}

void B4MTraces::ReceivedMessages(pair<float, int> new_value){
//  cerr << "Received " << new_value.second << " messages at " << new_value.first <<
//    "s" << endl;
  if (received_messages.count(new_value.first) >= 1)
    received_messages[new_value.first] += new_value.second;
  else
    received_messages.insert(new_value);

}

void B4MTraces::SentMessages(pair<float, int> new_value){
//  cerr << "Send " << new_value.second << " messages at " << new_value.first <<
//    "s" << endl;
  if (sent_messages.count(new_value.first) >= 1)
    sent_messages[new_value.first] += new_value.second;
  else
    sent_messages.insert(new_value);

}

void B4MTraces::DroppedMessages(pair<float, int> new_value){
  if (dropped_messages.count(new_value.first) >= 1)
    dropped_messages[new_value.first] += new_value.second;
  else
    dropped_messages.insert(new_value);
}
string B4MTraces::PrintSummary(){
  ostringstream ret;

  int total_bytes_received = 0;
  int total_bytes_sent = 0;
  int total_messages_received = 0;
  int total_messages_sent = 0;

  for (auto r : received_bytes)
    total_bytes_received += r.second;

  for (auto r : sent_bytes)
    total_bytes_sent += r.second;

  for (auto r : received_messages)
    total_messages_received += r.second;

  for (auto r : sent_messages)
    total_messages_sent += r.second;

  ret << "Total bytes received : " << total_bytes_received << endl;
  ret << "Total bytes sent : " << total_bytes_sent << endl;
  ret << "Total messages received : " << total_messages_received << endl;
  ret << "Total messages sent : " << total_messages_sent << endl;

  return ret.str();
}

string B4MTraces::PrintRaftSummary(){
  ostringstream ret;

  int election_number = 0;
  float delay = 0;

  for (auto d : election_delay){
    election_number += 1;
    delay += d.second;
  }

  int config_number = 0;
  float config_delay = 0;
  for (auto d : config_change_delay){
    config_number += 1;
    config_delay += d.second;
  }

  ret << "Number of elections : " << election_number << endl;
  ret << "Average election delay : " << delay / election_number << endl;
  ret << "Number of configuration change : " << config_number << endl;
  ret << "Average configuration change delay : " << config_delay / config_number << endl;

  return ret.str();
}

void B4MTraces::ExportResults(){

  // Exports block creation timing information
  ofstream output_file;
  char filename[50];
  sprintf(filename, "scratch/b4mesh/Traces/BlockInfo.txt");
  output_file.open(filename, ios::out);
  output_file << "#BlockHash" << " " << "GroupId" << " " << "NumTxs" << " " << "CreationTime" << endl;
  for (auto &it : block_creation){
    output_file << it.first.first << " " << it.first.second;
    output_file << " " << it.second.first << " " << it.second.second << endl;
  }
  output_file.close();

}
