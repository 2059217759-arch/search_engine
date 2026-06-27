CXX := g++
CXXFLAGS := -std=c++17 -Wall -O2
INCLUDES := -I./include -I./third_party/spdlog/include -I/usr/local/include

SRCDIR := src
OBJDIR := build
BINDIR := bin

SERVER_TARGET := $(BINDIR)/main
OFFLINE_TARGET := $(BINDIR)/offline

SERVER_OBJS := $(OBJDIR)/SearchServer.o $(OBJDIR)/SearchService.o $(OBJDIR)/KeywordRecommender.o \
               $(OBJDIR)/DenseRetriever.o $(OBJDIR)/QueryCache.o $(OBJDIR)/HotTracker.o \
               $(OBJDIR)/DocCache.o
OFFLINE_OBJS := $(OBJDIR)/offline.o $(OBJDIR)/DirectoryScanner.o \
                $(OBJDIR)/KeywordProcessor.o $(OBJDIR)/PageProcessor.o \
                $(OBJDIR)/KeywordRecommender.o

SERVER_LDFLAGS := -lpthread -L/usr/local/lib -ltinyxml2 -lworkflow -lwfrest -lcurl
OFFLINE_LDFLAGS := -lpthread -L/usr/local/lib -ltinyxml2

.PHONY: all clean

all: $(SERVER_TARGET) $(OFFLINE_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(SERVER_LDFLAGS)

$(OFFLINE_TARGET): $(OFFLINE_OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(OFFLINE_LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cc | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(OBJDIR) $(BINDIR)

clean-data:
	rm -f data/*.dat
