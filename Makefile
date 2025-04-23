
NS_BUILD_DIR = /home/laube/dev/ns3/ns-allinone-3.30.1/ns-3.30.1/build

INCLUDEPATH += $(NS_BUILD_DIR)
DEPENDPATH += $(BUILD_DIR)

LIBS += -L$(NS_BUILD_DIR)/lib

LIST = `ls $(NS_BUILD_DIR)/lib | sed -e "s/.so//g" | sed -e "s/lib/-l/g"`

#LIBS += $(LIST)

#all:
#	$(LIST)

CPP = g++
CPPFLAGS ?= -Wall -std=c++14 -I$(INCLUDEPATH) $(LIBS)
CPPFLAGS += -m64 -pipe -O2

DEFINES = -DNS3_LOG_ENABLE -DHAVE_SYS_IOCTL_H=1 -DHAVE_IF_NETS_H=1 -DHAVE_NET_ETHERNET_H=1 -DHAVE_PACKET_H=1 -DHAVE_IF_TUN_H=1

CPPFLAGS += -W -D_REENTRANT -fPIE $(DEFINES)

#LDLIBS ?= 





PROG ?= b4mesh 

OBJS ?= application_packet.o\
	experiment.o\
	b4mesh.o\
	b4mesh-helper.o\
	raft.o\
	raft-helper.o\
	block.o\
	transaction.o\
	blockgraph.o\
	b4mesh-mobility.o\
	b4mesh-mobility-helper.o\
	b4mesh-raft.o\
	b4m_traces.o\
  b4mesh-raft-helper.o

all: $(PROG)

b4mesh: $(OBJS) main.o
	$(CPP) $(CPPFLAGS) $(LIST) $(LDLIBS) -o b4mesh main.o $(OBJS) $(LDLIBS)

src/.cpp.o:
	$(CPP) $(CPPFLAGS) -c $*.cpp

src/.cc.o:
	$(CPP) $(CPPFLAGS) -c $*.cpp
clean:
	rm *.o $(PROG)
