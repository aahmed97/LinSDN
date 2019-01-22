#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define RESERVED_ARG "cont"
#define PROGRAM_NAME "./a2sdn"
#define MAX_SWITCHES 7
#define DELAY "delay"

struct flow_table{
  int srcIP_lo;
  int srcIP_hi;
  int destIP_lo;
  int destIP_hi;
  char actionType;
  int actionVal;
  int pri;
  int pktCount;
  int action_count;
  int sw;
  int src;
};

  struct controller_stats{
    int open;
    int query;
    int ack;
    int add;
  };

  struct current_switches{
    int low_ip;
    int high_ip;
    int left_neighbor;
    int right_neighbor;
    int sw_num;
    int active;
  };

  struct switch_stats{
    int admit;
    int ack;
    int addrule;
    int relayin;
    int open;
    int query;
    int relayout;
  };

  void init_controller(char * str_num_switch, char * port);
  void controller_loop(struct pollfd * fds, struct current_switches * switches, struct controller_stats cont_stats, int num_switches, int port_num);
  void query_switches(char * action, char * arguments[], struct current_switches * switches, int num_switches, int sw);
  void init_switch(char *arguments[]);
  void switch_loop(struct pollfd * fds,struct switch_stats stats, int switch_num, int n_left, int n_right, char * traffic_file, int ip_low, int ip_high);
  int execute_cont_rule(struct switch_stats * stats,char m[], int index, int n_left, int n_right, int sw_num, struct flow_table * ft);
  int update_flow_table(char m[], int dest_ip, struct flow_table * ft, int index,int to, int sw_num);
  int extract_sw_num(char * string);
  void remove_newline(char * string);
  int str_to_i(char * string);
  char * is_num(char * string);
  void print_controller(struct current_switches * swi, struct controller_stats stats);
  void print_switch(struct flow_table * ft, struct switch_stats stats, int index);
  int is_dig_str(char * str);


  int main(int argc, char *argv[]){
    if (strcmp(argv[1], RESERVED_ARG) == 0){
      if(is_num(argv[2])) {
        init_controller(argv[2],argv[3]);
      }
      else{
        printf("Command line arguments error\n");
        exit(0);
      }
    }
    else if (argc == 8){
      init_switch(argv);
    }
    else{
      printf("Command line arguments error\n");
      exit(0);
    }
  }


  //Initialize the controller including the cont stats struct which holds the count of
  //  received and transmitted messages as well as the struct array of current_switches
  //    and sends these to the controller Loop
  // Parameters: the command line arguments
  // Returns: void
  void init_controller(char * str_num_switch, char * port) {
    printf("In controller\n");
    int port_num;
    int sock;
    char str[100];
    int num_switches = str_to_i(str_num_switch);
    struct current_switches *swi = malloc(sizeof(struct current_switches)*MAX_SWITCHES);
    if (swi == NULL){
      printf("Error allocating space for current_switches\n");
      exit(0);
    }
    port_num = str_to_i(port);
    for (int i = 0; i < MAX_SWITCHES; i++){
      swi[i].low_ip = -1;
      swi[i].high_ip = -1;
      swi[i].left_neighbor = -1;
      swi[i].right_neighbor = -1;
      swi[i].sw_num = -1;
      swi[i].active = -1;
    }
    struct controller_stats cont_stats;
    cont_stats.open = 0;
    cont_stats.query = 0;
    cont_stats.ack = 0;
    cont_stats.add = 0;

    int return_val;
    if ((sock = socket(AF_INET,SOCK_STREAM,0)) < 0 ){
      printf("Error creating the controller socket\n");
      exit(0);
    }
    printf("Controller socket created\n");
    struct pollfd fds[num_switches+1];
    fds[0].fd = sock;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    for(int i = 1; i < num_switches+1; i++){
        fds[i].fd = -1;
        fds[i].events = POLLIN;
        fds[i].revents = 0;
      }
    controller_loop(fds,swi, cont_stats, num_switches,port_num);
  }

  // Polls the file descriptors for the fifos to check for messages from switches and executes the action
  // corresponding to the agents message. Also polls the stdin file descriptor and either lists the current
  // switches information and the counts for received and transmitted messages, lists then exits or
  // does nothing with input if it is invalid.
  // Parameters: fds: array of pollfd structs for reading messages, current_switches: array of structures containing info on current active switches, cont_stats: array of structures containing counts of received and transmitted messages, num_switches: number of switches specified in command line
  // Returns: void
  void controller_loop(struct pollfd * fds, struct current_switches * switches, struct controller_stats cont_stats, int num_switches, int port_num) {
    int fd;
    int o = 1;
    int fromlen;
    int active_sockets = 1;
    int return_val;
    char command[100];
    char str[100];
    int sock = fds[0].fd;
    struct sockaddr_in sin, from;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port_num);
    setsockopt(sock,SOL_SOCKET, SO_REUSEPORT,&o,sizeof(o));
    printf("Binding to port %d.....\n", port_num);
    if(bind(sock,(struct sockaddr *)&sin,sizeof(sin))<0 ){
      sleep(2);
      for (int i = 0; i < 3; i++){
        if(bind(sock,(struct sockaddr *)&sin,sizeof(sin))>0 ){
          break;
        }
        sleep(2);
      }
      printf("Error binding socket\n");
      exit(0);
    }
    printf("Listening to %d switches\n", num_switches);
    if (listen(sock,num_switches) < 0){
      printf("Error in listening to socket connections\n");
      exit(0);
    }
    while(1){
      struct pollfd fd[1];
      fd[0].fd = STDIN_FILENO;
      fd[0].events = POLLIN;
      fd[0].revents = 0;
      memset(command,0,100);
      return_val = poll(fd,1,1);
      if (POLLIN & fd[0].revents){
        int length = read(fd[0].fd,command,100);
        if(strcmp(command,"list\n") == 0){
          print_controller(switches, cont_stats);
        }
        if(strcmp(command,"exit\n") == 0){
          print_controller(switches, cont_stats);
          for (int i = 0; i < num_switches; i++){
            close(fd[i].fd);
          }
          free(switches);
          exit(0);
        }
      }
      return_val = 0;
      return_val = poll(fds,active_sockets,0);
      //newly added
      char * arg[5];
      char * token;
      if ((active_sockets<=num_switches) && (fds[0].revents & POLLIN)) {
        fromlen = sizeof(from);
        printf("Accepting socket...\n");
        fds[active_sockets].fd = accept(sock,(struct sockaddr *)&from,&fromlen);
        fds[active_sockets].events = POLLIN;
        fds[active_sockets++].revents = 0;
        printf("Done\n");
      }
      for (int i = 1; i < active_sockets; i++){
        //printf("Checking %d socket\n", i);
        if (fds[i].revents & POLLIN){
          char packet[1000];
          memset(packet,0,1000);
          int ret = read(fds[i].fd,packet,1000);
          if (ret == 0){
            close(fds[i].fd);
            fds[i].fd = -1;
            printf("lost connection to switch %d\n",switches[i-1].sw_num);
            switches[i-1].active = -1;
          }
          else{
          char delim[2] = " ";
          token = strtok(packet,delim);
          if(strcmp("open",token) ==0){
            int ind = 0;
            while ((token = strtok(NULL,delim)) !=NULL) {
              arg[ind++] = token;
            }
            cont_stats.ack++;
            cont_stats.open++;
            switches[i-1].low_ip = str_to_i(arg[2]);
            switches[i-1].high_ip = str_to_i(arg[3]);
            switches[i-1].left_neighbor = str_to_i(arg[0]);
            switches[i-1].right_neighbor = str_to_i(arg[1]);
            switches[i-1].sw_num = str_to_i(arg[4]);
            switches[i-1].active = 1;
            printf("Received (src= sw%d, dest= cont) [OPEN]\n",switches[i-1].sw_num);
            char l_neighbour[20];
            char r_neighbour[20];
            char l_n_sw[5];
            char r_n_sw[5];
            strcpy(l_n_sw,"sw");
            strcpy(r_n_sw,"sw");
            if (*arg[0] == '-'){
              strcpy(l_neighbour,"null");
            }
            else{
              char * s= strcat(l_n_sw,arg[0]);
              strcpy(l_neighbour,s);
            }
            if(*arg[1] == '-'){
              strcpy(r_neighbour,"null");
            }
            else{
              char * s = strcat(r_n_sw,arg[1]);
              strcpy(r_neighbour,s);
            }
            printf("\tport0=cont, port1= %s, port2= %s, port3= %s-%s\n",l_neighbour,r_neighbour,arg[2],arg[3]);
            char * ack = "ack";
            send(fds[i].fd,ack,strlen(ack),0);
            printf("Transmitted (src= cont, dest= sw%s)[ACK]\n",arg[4]);
          }
          else if (strcmp("query",token)==0 ){
              char action[100];
              int ind = 0;
              cont_stats.add++;
              cont_stats.query++;
              while ((token = strtok(NULL,delim)) != NULL){
                arg[ind++] = token;
              }
              printf("Received (src= sw%d, dest= cont)[QUERY]: header= (srcIP= %s, destIP= %s)\n",switches[i-1].sw_num,arg[1],arg[0]);
              query_switches(action,arg, switches, num_switches, switches[i-1].sw_num);
              strcat(action, " ");
              strcat(action,arg[1]);
              send(fds[i].fd,action,strlen(action),0);
            }
          }
        }
      }
      }
    }

  // Compares the queried destination IP received from child and stores in the string action
  // either a forward message if the destination exists for some switch in the current switches,
  // else it stores a drop message.
  // Parameters: char * action: for storing the action to be taken, char *arguments[]: command line arguments,
  //    struct current_switches * switches: struct containing current active switches, int num_switches: the number of switches,
  //       int sw_num: the switch number which sent the query
  // Returns: void
  void query_switches(char * action,char * arguments[], struct current_switches * switches, int num_switches, int sw) {
    int ip;
    ip = str_to_i(arguments[0]);
    sprintf(action, "drop %d",ip);
    for (int i = 0; i < num_switches; i++){
      if (switches[i].low_ip  != -1){
        if((ip<=switches[i].high_ip) && (ip >= switches[i].low_ip)){
          memset(action,0,100);
          sprintf(action, "f %d %d %d %d",switches[i].sw_num, switches[i].low_ip, switches[i].high_ip, ip);
          printf("Transmitted (src= cont, dest= sw%d)[ADD]:\n",sw);
          if (switches[i].sw_num<sw){
            printf("\t(srcIP= 0-1000, destIP=%d-%d, action= FORWARD:1, pri=4, pktCount= 0)\n", switches[i].low_ip,switches[i].high_ip);
          }
          else{
            printf("\t(srcIP= 0-1000, destIP=%d-%d, action= FORWARD:2, pri=4, pktCount= 0)\n", switches[i].low_ip,switches[i].high_ip);
          }
        }
      }
    }
    if (action[0]=='d'){
      printf("Transmitted (src= cont, dest= sw%d)[ADD]:\n",sw);
      printf("\t(srcIP= 0-1000, destIP=%d-%d, action= DROP:0, pri=4, pktCount= 0)\n",ip,ip);
    }
   }

  // Extracts the information from the command line arguments and sends an open to the
  // controller using the extracted info in its message. Initializes the struct containing the counts
  // of received and transmitted messages for the switch then calls switch loop
  //  Parameters: arguments[]: the command line arguments
  //  Returns: void
  void init_switch(char *arguments[]){
    printf("In switch\n");
    struct sockaddr_in addr;
    struct sockaddr_in s_addr;
    memset(&s_addr,0,sizeof(s_addr));
    struct hostent * serv;
    int port = str_to_i(arguments[7]);
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port);
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    if (sockfd<0){
      printf("There was an error creating switch socket\n");
      exit(0);
    }
    //dont know what to do here
    if (!is_dig_str(arguments[6])){
      serv = gethostbyname(arguments[6]);
      char * s = serv->h_addr_list[0];
      inet_pton(AF_INET,s,&s_addr.sin_addr);
    }
    else{
      inet_pton(AF_INET,arguments[6],&s_addr.sin_addr);
    }
    //end of dont kno
    //going to fix this using the command line arguments
    //end of need to
    printf("Connecting to controller....\n");
    if (connect(sockfd,(struct sockaddr *)&s_addr,sizeof(s_addr)) < 0){
      sleep(2);
      for (int i = 0; i < 3; i++){
        if (connect(sockfd,(struct sockaddr *)&s_addr,sizeof(s_addr)) > 0){
          break;
        }
        sleep(2);
      }
      printf("Could not connect\n");
      exit(0);
    }
    printf("Done\n");
    int fd;
    int switch_num;
    char delim[2] = "-";
    char * token;
    char * traffic_file;
    int ip_low;
    int ip_high;
    int neighbour_left;
    int neighbour_right;
    char ip_range[20];
    strcpy(ip_range,arguments[5]);
    token = strtok(arguments[5], delim);
    if(is_num(token)){
      ip_low = str_to_i(token);
    }
    else{
      printf("Command line arguments error\n");
      exit(0);
    }
    token = strtok(NULL, delim);
    if (is_num(token)){
      ip_high = str_to_i(token);
    }
    else{
      printf("Command line arguments error\n");
      exit(0);
    }
    neighbour_left = extract_sw_num(arguments[3]);
    neighbour_right = extract_sw_num(arguments[4]);
    traffic_file = arguments[2];
    switch_num = extract_sw_num(arguments[1]);
    if (switch_num == -1){
      printf("Command line arguments error\n");
      exit(0);
    }
    char open_message[100];
    char str[100];
    memset(open_message,0,100);
    sprintf(open_message, "open %d %d %d %d %d", neighbour_left, neighbour_right, ip_low, ip_high,switch_num);
    //dont know if i should use the flag MSG_DONTWAIT instead of 0
    printf("Transmitted (src= sw%d, dest= cont)[OPEN]:\n",switch_num);
    printf("\t(port0= cont, port1= %s, port2=%s, port3= %s)\n",arguments[3],arguments[4],ip_range);
    send(sockfd,open_message,strlen(open_message),0);
    struct switch_stats stats;
    stats.admit = 0;
    stats.ack = 0;
    stats.addrule = 0;
    stats.relayin = 0;
    stats.open = 1;
    stats.query = 0;
    stats.relayout = 0;
    memset(str,0,100);
    struct pollfd fds[3];
    sprintf(str, "fifo-%d-%d", neighbour_left, switch_num);
    fds[0].fd = open(str, O_RDONLY | O_NONBLOCK );
    memset(str,0,100);
    sprintf(str, "fifo-%d-%d", neighbour_right, switch_num);
    fds[1].fd = open(str, O_RDONLY | O_NONBLOCK );
    memset(str,0,100);
    fds[2].fd = sockfd;
    memset(str,0,100);
    for (int i = 0; i < 3; i++){
      fds[i].events = POLLIN;
      fds[i].revents = 0;
    }
    switch_loop(fds,stats, switch_num, neighbour_left, neighbour_right, traffic_file, ip_low, ip_high);
   }

   // Polls the file descriptors to check for messages from the controller, stdin, or any of its
   // neighbouring switches. If list is passed into stdin the flow table and counts of received and
   // transmitted messages are printed, if exit is passed then the same information is listed
   // and the program exits. If there is a message from neighbours then it either relays it to
   // the next neighbour or adds it to its own flow table.
   // Parameters: fds: pollfd struct containing file descriptors, stats: the count of rec/trans messages,
   //    int switch num: the switch number, int n_left: number for left neighbour of switch, int n right:
   //      number for right neighbour, char * traffic file: the string rep of traffic file, int ip high: dest_IP high
   //        int ip_low: dest_IP_low
   //  Returns: void
   void switch_loop(struct pollfd *fds, struct switch_stats stats, int switch_num, int n_left, int n_right, char * traffic_file, int ip_low, int ip_high) {
     struct flow_table * ft = malloc(sizeof(struct flow_table) * 100);
     int delay = 0;
     int delaying = 0;
     time_t initial;
     time_t current;
     char src_ip_str[10];
     time_t diff;
     int msec;
     if (ft == NULL){
       printf("Could not allocate the flow table\n");
       exit(0);
     }
     int fd;
     ft[0].srcIP_lo = 0;
     ft[0].srcIP_hi = 1000;
     ft[0].destIP_lo = ip_low;
     ft[0].destIP_hi = ip_high;
     ft[0].actionType = 'f';
     ft[0].pri = 4;
     ft[0].pktCount = 0;
     ft[0].action_count = 3;
     int index = 0;
     char str[100];
     memset(str,0,100);
     int src_ip;
     int dest_ip;
     int added = 0;
     int t_file_closed = 0;
     FILE * fp = fopen(traffic_file, "r");
     while(1){
       char line[1000];
       char sw_name[10];
       sprintf(sw_name, "sw%d", switch_num);
       if (delaying){
         current = time(NULL);
         diff = current-initial;
         //printf("Time elapsed %f\n",diff*100);
         if (diff> delay/1000){
           delaying = 0;
           delay = 0;
           printf("Delay over\n");
         }
       }
       if (!delaying){
       if (fgets(line, 1000, fp) != NULL && !t_file_closed) {
           char f = line[0];
             if (f!='#'){
               char * token;
               char * t2;
               char delim[2] = " ";
               token = strtok(line, delim);
               if (strcmp(token,sw_name) == 0){
                 token = strtok(NULL,delim);
                 if (strcmp(token,DELAY)!=0){
                   stats.admit++;
                   if (is_num(token)){
                     src_ip = str_to_i(token);
                     memset(src_ip_str,0,10);
                     strcpy(src_ip_str,token);
                   }
                   if (src_ip>=0 && src_ip<=1000){
                   token = strtok(NULL, delim);
                   int len = strlen(token)+1;
                   char dest[len];
                   strcpy(dest, token);
                   dest_ip = str_to_i(token);
               for (int k = 1; k <=index; k++){
                 if (((dest_ip <= ft[k].destIP_hi) && (dest_ip >= ft[k].destIP_lo)) && (ft[k].actionType == 'f')) {
                   ft[k].pktCount++;
                   stats.relayout++;
                   memset(str,0,100);
                   char action[100];
                   memset(action,0,100);
                   int to = 0;
                   if (ft[k].sw < switch_num){
                     to = n_left;
                   }
                   else {
                     to = n_right;
                   }
                   sprintf(action,"f %d %d %d %d %d",ft[k].sw,ft[k].destIP_lo,ft[k].destIP_hi,dest_ip,ft[k].src);
                   sprintf(str,"fifo-%d-%d",switch_num,to);
                   fd = open(str,O_WRONLY|O_NONBLOCK);
                   sleep(1);
                   int len = write(fd, action,strlen(action)+1);
                   printf("Transmitted (src= sw%d, dest= sw%d)[RELAY]: header= (srcIP= %s, destIP=%d)\n",switch_num,ft[k].sw,src_ip_str,dest_ip);
                   added = 1;
                   break;
                 }
               }
               if ((dest_ip <= ft[0].destIP_hi) && (dest_ip >= ft[0].destIP_lo)){
                 ft[0].pktCount++;
                 added = 1;
               }
               for (int k = 1; k < index; k++){
                 if (ft[k].destIP_lo==dest_ip && ft[k].destIP_hi==dest_ip && ft[k].actionType=='d'){
                   ft[k].pktCount++;
                   added = 1;
                 }
               }
               if(!added){
                   char message[100];
                   char sw_name[100];
                   memset(message,0,100);
                   memset(sw_name,0,100);
                   strcpy(message,"query ");
                   remove_newline(dest);
                   strcat(message,dest);
                   strcat(message," ");
                   strcat(message,src_ip_str);
                   printf("Transmitted (src= sw%d, dest= cont)[QUERY]: header= (srcIP= %s, destIP= %d)\n",switch_num, src_ip_str,dest_ip);
                   //sprintf(sw_name, "fifo-%d-0", switch_num);
                   //fd = open(sw_name,O_WRONLY|O_NONBLOCK);
                   int ret = send(fds[2].fd, message, strlen(message)+1,0);
                    //close(fd);
                   stats.query++;
                 }
               }
             }
               else if (strcmp(DELAY,token) == 0){
                 char delay_str[100];
                 memset(delay_str,0,100);
                 token = strtok(NULL,delim);
                 delay = str_to_i(token);
                 delaying = 1;
                 initial = time(NULL);
                 printf("Entering a delay period of %dmsec\n",delay);
               }
             }
           }
         }
       }
           else if (!t_file_closed && !delaying){
             fclose(fp);
             t_file_closed = 1;
           }

           added = 0;
           struct pollfd fds_stdin[1];
           int return_val;
           fds_stdin[0].fd = STDIN_FILENO;
           fds_stdin[0].events = POLLIN;
           fds_stdin[0].revents = 0;
           return_val = poll(fds_stdin, 1, 1);
           if (return_val && (POLLIN & fds_stdin[0].revents)){
             char command[100];
             memset(command,0,100);
             int len = read(fds_stdin[0].fd,command,100);
             if (strcmp(command,"list\n") == 0){
               print_switch(ft,stats,index);
             }
             if (strcmp(command,"exit\n") == 0){
               print_switch(ft,stats,index);
               for(int i = 0; i < 3; i++){
                 close(fds[i].fd);
               }
               free(ft);
               exit(0);
             }
           }
           return_val = poll(fds,3,10);
           if (return_val){
             for (int i = 0; i < 3; i++){
               if (fds[i].revents & POLLIN) {
                 if (i == 0){
                   int length;
                   char packet[100];
                   memset(packet,0,100);
                   length = read(fds[i].fd, packet,100);
                   char p[100];
                   char delim[2] = " ";
                   strcpy(p,packet);
                   char f_char = packet[0];
                   int f_sw = f_char - '0';
                   char * token;
                   token = strtok(p,delim);
                   token = strtok(NULL,delim);
                   token = strtok(NULL,delim);
                   token = strtok(NULL,delim);
                   token = strtok(NULL,delim);
                   int dest = str_to_i(token);
                   token = strtok(NULL,delim);
                   printf("Received (src=sw%d, dest= sw%d) [RELAY]: header= (srcIP= %s, destIP= %d)\n",switch_num-1,switch_num,token,dest);
                   if (dest < ip_low || dest > ip_high){
                     stats.relayin++;
                     stats.relayout++;
                     for (int w = 0; w <= index; w++){
                       if ((dest >= ft[w].destIP_lo) && (dest<=ft[w].destIP_hi)){
                         ft[w].pktCount++;
                       }
                     }
                     char message[100];
                     memset(message,0,100);
                     memset(str,0,100);
                     sprintf(str,"fifo-%d-%d",switch_num,switch_num+1);
                     int fd = open(str,O_WRONLY|O_NONBLOCK);
                     int ret = write(fd,packet,strlen(packet)+1);
                     printf("Transmitted (src= sw%d, dest= sw%d) [RELAY]: header= (srcIP= %s, destIP= %d)\n",switch_num, switch_num+1, token,dest);
                     close(fd);
                   }
                   else{
                     stats.relayin++;
                     ft[0].pktCount++;
                   }
                 }
                 else if (i == 1) {
                   int length;
                   char packet[100];
                   memset(packet,0,100);
                   length = read(fds[i].fd, packet,100);
                   char f_char = packet[0];
                   int f_sw = f_char - '0';
                   char p[100];
                   char delim[2] = " ";
                   strcpy(p,packet);
                   f_char = packet[0];
                   f_sw = f_char - '0';
                   char * token;
                   token = strtok(p,delim);
                   token = strtok(NULL,delim);
                   token = strtok(NULL,delim);
                   token = strtok(NULL,delim);
                   token = strtok(NULL,delim);
                   int dest = str_to_i(token);
                   token = strtok(NULL,delim);
                   printf("Received (src= sw%d, dest= sw%d) [RELAY]: header= (srcIP= %s, destIP= %d)\n",switch_num+1,switch_num, token, dest);
                   if (dest < ip_low || dest > ip_high) {
                     stats.relayin++;
                     stats.relayout++;
                     for (int w = 0; w <= index; w++){
                       if ((dest >= ft[w].destIP_lo) && (dest<=ft[w].destIP_hi)){
                         ft[w].pktCount++;
                       }
                     }
                     char message[100];
                     memset(str,0,100);
                     memset(message,0,100);
                     memset(str,0,100);
                     sprintf(str,"fifo-%d-%d",switch_num,switch_num-1);
                     int fd = open(str,O_WRONLY|O_NONBLOCK);
                     int ret = write(fd,packet,strlen(packet)+1);
                     printf("Transmitted (src= sw%d, dest= sw%d) [RELAY]: header= (srcIP= %s, destIP= %d)\n",switch_num, switch_num-1,token,dest);
                     close(fd);
                   }
                   else{
                     stats.relayin++;
                     ft[0].pktCount++;
                   }
                 }
                 else {
                   char packet[100];
                   memset(packet,0,100);
                   int length = read(fds[i].fd,packet,100);
                   if (length == 0){
                     printf("Controller has closed\n");
                     close(fds[i].fd);
                     fds[i].fd = -1;
                   }
                   else if (packet[0] == 'a'){
                     stats.ack++;
                     printf("Received (src= cont, dest= sw%d)[ACK]\n",switch_num);
                   }
                   else{
                     stats.addrule++;
                     char p[100];
                     char * token;
                     memset(p,0,100);
                     strcpy(p,packet);
                     char delim[2] = " ";
                     if (p[0] == 'f'){
                       token = strtok(p,delim);
                       token = strtok(NULL,delim);
                       int dest_switch = str_to_i(token);
                       token = strtok(NULL,delim);
                       int dest_low = str_to_i(token);
                       token = strtok(NULL,delim);
                       int dest_high = str_to_i(token);
                       printf("Received (src= cont, dest= sw%d)[ADD]\n",switch_num);
                       if (dest_switch<switch_num){
                         printf("\t(srcIP=0-1000, destIP=%d-%d, action=FORWARD:1, pri=4, pktCount=0)\n",dest_low, dest_high);
                       }
                       else{
                         printf("\t(srcIP=0-1000, destIP=%d-%d, action=FORWARD:2, pri=4, pktCount=0)\n",dest_low, dest_high);
                       }
                     }
                     else{
                       token = strtok(p,delim);
                       token = strtok(NULL,delim);
                       int dest_both = str_to_i(token);
                       printf("Received (src= cont, dest= sw%d)[ADD]\n",switch_num);
                       printf("\t(srcIP=0-1000, destIP=%d-%d, action=DROP:0, pri=4, pktCount=0)\n",dest_both, dest_both);
                     }
                     int added = execute_cont_rule(&stats,packet,index,n_left,n_right, switch_num,ft);
                     index+=added;
                   }
                 }
               }
             }
           }
         }
       }


        // Checks if a string contains only digit characters
        //   Parameters: string: ptr to string to be parsed
        //   Returns: string: ptr to input string if contains digit char or null if there are other
        //        non digit chars
        char * is_num(char * string) {
            char *ptr = string;
            while(*ptr != '\0'){
                if((*ptr<'0')||(*ptr>'9')){
                    ptr = NULL;
                    return ptr;
                }
                ptr++;
            }
            return string;
        }

        //Converts a string representation of an integer into an int
        // Parameters: string: ptr to string to be converted
        // Returns: num: the integer number the string contained
        int str_to_i(char * string) {
            int num = 0;
            char *ptr = string;
            if (*ptr == '-'){
              return -1;
            }
            while(*ptr != '\0'){
              if(isdigit(*ptr)){
                num = (num*10) + (*ptr-'0');
              }
              ptr++;
            }
            return num;
        }

        //Removes new line from the end of a string
        //  Parameters: string: ptr to string for newline to be removed from
        //  Returns: void
        void remove_newline(char * string){
          char * ptr = string;
          while(*ptr != '\0'){
            if (*ptr == '\n')
              *(ptr++) = '\0';
            else
              ptr++;
          }
          return;
        }

        // Takes in the string of form swk where k is 1,2,…,7 and extracts the k in the form
        // of an integer and returns k.
        //  Parameters: char * string: string in the form of swk, kE[1,2,3,...,7]
        //  Returns: int num: k in int format
        int extract_sw_num(char * string){
          char * ptr = string;
          int num = -1;
          if (strcmp(string,"null") == 0){
            return num;
          }
          while (*ptr != '\0'){
            if ((*ptr <= '7') && (*ptr >='1')) {
              char ch = *ptr;
              num = ch - '0';
            }
            ptr++;
          }
          if(num < 0)
            num = -1;
          return num;
        }

        // Prints the flow table of the switch and prints the counts of the received and transmitted messages.
        //  Parameters: struct flow_table * ft: the flow table for the switch,
        //    struct switch_stats stats: count of rec/transmit messages, int index:index of flow table
        // Returns: void
        void print_switch(struct flow_table * ft, struct switch_stats stats, int index){
          printf("Flow table:\n");
          printf("-----------------------------------------------------------------------------------\n");
          char a[100];
          for (int i = 0; i <= index; i++) {
            if (ft[i].actionType == 'f'){
              strcpy(a,"FORWARD");
            }
            else if (ft[i].actionType == 'd'){
              strcpy(a,"DROP");
            }
              printf("[%d]  (srcIP = %d-%d, destIP= %d-%d, action= %s:%d, pri=%d, pktCount=%d)\n",i,ft[i].srcIP_lo,ft[i].srcIP_hi,ft[i].destIP_lo,ft[i].destIP_hi,a,ft[i].action_count,ft[i].pri,ft[i].pktCount);
          }
          printf("\tReceived:\tADMIT:%d, ACK:%d, ADDRULE:%d, RELAYIN:%d\n",stats.admit,stats.ack,stats.addrule,stats.relayin);
          printf("\tTransmitted: OPEN:%d, QUERY:%d, RELAYOUT:%d\n",stats.open,stats.query,stats.relayout);
          printf("-----------------------------------------------------------------------------------\n");
        }

        // Prints the information on the currently active switches as well as counts of the received
        // and transmitted messages.
        //  Parameters: struct current_switches * swi: struct containing info on currently active switches
        //    struct controller_stats stats: counts of rec/transmit messages for controller
        //  Returns: void
        void print_controller(struct current_switches * swi, struct controller_stats stats) {
          printf("\n");
          printf("Switch information: \n");
          for (int i = 0; i < MAX_SWITCHES; i++){
            if (swi[i].low_ip >= 0){
              printf("[sw%d] port1= %d, port2= %d, port3= %d-%d\n",swi[i].sw_num,swi[i].left_neighbor,swi[i].right_neighbor,swi[i].low_ip,swi[i].high_ip);
            }
          }
          printf("\n");
          printf("Packet Stats:\n");
          printf("\tReceived:\tOPEN:%d, QUERY:%d\n",stats.open,stats.query);
          printf("\tTransmitted: ACK:%d, ADD:%d\n",stats.ack,stats.add);
        }

        // Updates the flow table to either add to an entry which already exists or to add the new entry if it does not exist already.
        // Parameters: m: contains the action, dest_ip: the dest_ip for the message, ft: the flow table,
        //      index: index of the flow table, to: contains the message destination switch
        //  Returns: 1 if new entry is added, else 0 if an old entry is updated
        int update_flow_table(char m[], int dest_ip, struct flow_table * ft, int index, int to, int sw_num) {
          char delim[2] = " ";
          char * token;
          int added = 0;
          int new_entry = 0;
          char first_char = m[0];
          if (first_char == 'f'){
          token = strtok(m,delim);
          token = strtok(NULL,delim);
          token = strtok(NULL,delim);
          int low_ip = str_to_i(token);
          token = strtok(NULL,delim);
          int high_ip = str_to_i(token);
          token = strtok(NULL,delim);
          token = strtok(NULL,delim);
          int src = str_to_i(token);
          for (int i = 0; i <= index; i++){
            if ((low_ip == ft[i].destIP_lo) && (high_ip == ft[i].destIP_hi) && (ft[i].actionType == 'f')){
              ft[i].pktCount++;
              added = 1;
              break;
            }
          }
          if (!added){
            ft[index+1].srcIP_lo = 0;
            ft[index+1].srcIP_hi = 1000;
            ft[index+1].destIP_lo = low_ip;
            ft[index+1].destIP_hi = high_ip;
            ft[index+1].actionType = 'f';
            ft[index+1].pri = 4;
            ft[index+1].pktCount = 1;
            if (to>sw_num){
              ft[index+1].action_count = 2;
            }
            else{
              ft[index+1].action_count = 1;
            }
            ft[index+1].sw = to;
            ft[index+1].src =src;
            new_entry  = 1;
          }
        }
        else{
          for (int i = 0; i <= index; i++){
            if ((ft[i].destIP_lo == dest_ip) && (ft[i].destIP_hi == dest_ip) && ft[i].actionType == 'd'){
              ft[i].pktCount++;
              added = 1;
              break;
            }
          }
          token = strtok(m,delim);
          token = strtok(NULL,delim);
          token = strtok(NULL,delim);
          int src = str_to_i(token);
          if(!added){
            ft[index+1].srcIP_lo = 0;
            ft[index+1].srcIP_hi = 1000;
            ft[index+1].destIP_lo = dest_ip;
            ft[index+1].destIP_hi = dest_ip;
            ft[index+1].actionType = 'd';
            ft[index+1].pri = 4;
            ft[index+1].pktCount = 1;
            ft[index+1].action_count = 0;
            ft[index+1].src = src;
            new_entry = 1;
          }
        }
          return new_entry;
        }

        // Takes in the action message specified by the controller and then executes the specified action.
        // Calls update flow table to process the action and add it to the flow table.
        //  Parameters: stats: contains the rec/transmit message counts for the switch, m: the message containing the action to take,
        //    n_left: left neighbour switch number, n_right: right neighbour switch number, sw_num: switch number of switch that
        //      received the action, ft: the flow table
        int execute_cont_rule(struct switch_stats * stats, char m[], int index, int n_left, int n_right, int sw_num, struct flow_table * ft){
          int new_entry;
          char src[10];
          char first_char = m[0];
          int dest_ip = -1;
          if (first_char == 'd'){
            char delim[2] = " ";
            char k[100];
            strcpy(k,m);
            char * token;
            token = strtok(k, delim);
            token = strtok(NULL, delim);
            dest_ip = str_to_i(token);
            new_entry = update_flow_table(m,dest_ip,ft,index,0, sw_num);
          }
          else if (first_char == 'f'){
            int sw_to = m[2] - '0';
            stats->relayout++;
            char str[100];
            char w[100];
            memset(str,0,100);
            char * token;
            strcpy(w,m);
            char delim[2] = " ";
            int i = 1;
            token = strtok(w,delim);
            while((token = strtok(NULL,delim))!=NULL) {
              i++;
              if (i==2){
                int sw_to = str_to_i(token);
              }
              if (i == 5){
                dest_ip = str_to_i(token);
              }
              if (i == 6){
                strcpy(src,token);
              }
            }
            int to;
            if (sw_to < sw_num){
              to = n_left;
            }
            else{
              to = n_right;
            }
            sprintf(str,"fifo-%d-%d",sw_num,to);
            int fd = open(str,O_WRONLY|O_NONBLOCK);
            int ret = write(fd,m,strlen(m)+1);
            close(fd);
            printf("Transmitted (src= sw%d, dest= sw%d)[RELAY]: header= (srcIP= %s, destIP= %d)\n", sw_num,to,src,dest_ip);
            new_entry = update_flow_table(m,dest_ip,ft,index,sw_to,sw_num);
          }
          return new_entry;
        }


        int is_dig_str(char * str){
          int ret = 1;
          char * ptr = str;
          while (*ptr != '\0' && *ptr != '\n'){
            if ((*ptr>'9' || *ptr <'0') && (*ptr != '.')){
              ret = 0;
            }
            ptr++;
          }
          return ret;
        }
