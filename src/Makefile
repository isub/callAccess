UTL_DIR = ../../utils/src
EX_DEBUG =

callAccess: main.o ca.o ca_sip.o ca_conf.o ca_sync.o ca_planned_action.o ca_caller_pool.o ca_http_handler.o utl_log.o ca_conf.tab.o lex.ca_conf.o
	g++ -o ../callAccess main.o ca.o ca_sip.o ca_conf.o ca_sync.o ca_planned_action.o ca_caller_pool.o ca_http_handler.o utl_log.o ca_conf.tab.o lex.ca_conf.o -lpj -lpjsip -lpjlib-util -lpthread -L/usr/local/lib -lhttp_parser -ltcp_listener
main.o: main.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o main.o -c main.cpp
ca.o: ca.h ca.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o ca.o -c ca.cpp
ca_sip.o: ca_sip.h ca_sip.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o ca_sip.o -c ca_sip.cpp
ca_conf.o: ca_conf.h ca_conf.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o ca_conf.o -c ca_conf.cpp
ca_sync.o: ca_sync.h ca_sync.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o ca_sync.o -c ca_sync.cpp
ca_planned_action.o: ca_planned_action.h ca_planned_action.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o ca_planned_action.o -c ca_planned_action.cpp
ca_caller_pool.o: ca_caller_pool.h ca_caller_pool.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o ca_caller_pool.o -c ca_caller_pool.cpp
ca_http_handler.o: ca_http_handler.h ca_http_handler.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o ca_http_handler.o -c ca_http_handler.cpp
utl_log.o: $(UTL_DIR)/utils/log/log.h $(UTL_DIR)/utils/log/log.cpp
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o utl_log.o -c $(UTL_DIR)/utils/log/log.cpp
lex.ca_conf.c: ca_conf.l
	flex -Pca_conf -olex.ca_conf.c ca_conf.l
ca_conf.tab.c: ca_conf.y
	bison --name-prefix=ca_conf --defines --output-file=ca_conf.tab.c ca_conf.y
ca_conf.tab.o: ca_conf.tab.c
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o ca_conf.tab.o -c ca_conf.tab.c
lex.ca_conf.o: lex.ca_conf.c
	g++ $(EX_DEBUG) -I$(UTL_DIR) -o lex.ca_conf.o -c lex.ca_conf.c

clean:
	rm -f *.o

debug: clean debug-flags callAccess

debug-flags:
	export EX_DEBUG="-g -DDEBUG"
