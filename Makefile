GPP = g++
FLAGS = -O3 -g -mrtm
FLAGS += -fopenmp
## note: -mrtm says compile for a system with Intel RTM (restricted transactional memory)
#FLAGS += -DNDEBUG
LDFLAGS = -lpthread

all: htm_hello_world
all: benchmark_kcas
all: benchmark_set
	
%:
	$(GPP) $(FLAGS) -o $@.out $@.cpp $(LDFLAGS)

clean:
	rm -f *.out 
